/* lgp-closed-loop: drive a game from what its own memory says, with the libretro-guest-probe frame hook.
 *
 * The guest publishes a record of 32-bit words in its RAM (a struct whose address the toolchain's nm gives). Each frame
 * the hook reads the record and decides the pad: while the record's screen word is not the play value it presses the
 * menu buttons in turn, and once it is, it holds a fire button and weaves the player between two bounds with left and
 * right, reading the player's x (a float word) to know when to turn. It captures video and audio from the first frame
 * of play, counts the frames the core presented and repeated while playing, and checks what it saw at the end.
 *
 *   lgp-closed-loop [options] CORE CONTENT SYSTEM_DIR       (see --help)
 *
 * Nothing here knows a game: the words, values and buttons come from the command line.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

#define MAX_WORDS 1024u
#define MAX_MENU 8u
#define MENU_EVERY 30u /* frames between menu presses */
#define MENU_HOLD 4u   /* frames each press is held */

typedef struct Loop {
    /* settings */
    uint32_t record;     /* offset of the record in the guest memory */
    unsigned words;
    unsigned ready_word;
    uint32_t ready_value;
    unsigned screen_word;
    uint32_t play_screen;
    unsigned x_word;
    float x_min, x_max;
    int menu[MAX_MENU];
    unsigned menu_count;
    int fire;
    unsigned play_frames_wanted;
    unsigned capture_frames;
    /* what happened */
    uint32_t rec[MAX_WORDS];
    int record_out_of_range;
    unsigned menu_frames, menu_presses;
    int playing;
    unsigned play_from; /* host frame of the first frame in play */
    unsigned play_frames;
    int direction;
    unsigned turns;
    float x_low, x_high, x_first;
    /* counts over the frames in play, as the runs reported them */
    unsigned play_runs, play_presented, play_repeated, play_no_video, play_stalls, play_leaps, play_max_run_us;
} Loop;

static float word_float(uint32_t w)
{
    float f;
    memcpy(&f, &w, sizeof(f));
    return f;
}

static void hook(void *user, const LgpFrameInfo *info, LgpFrameControl *control)
{
    Loop *L = (Loop *)user;
    unsigned port;

    for (port = 0; port < info->ports; ++port) {
        control->joypad[port] = 0;
    }
    control->capture = L->playing && control->capture;
    if (!info->ready) {
        return;
    }
    if (!lgp_read_words(info->memory, info->memory_size, L->record, L->rec, L->words, 0)) {
        L->record_out_of_range = 1;
        control->stop = 1;
        return;
    }
    if (L->rec[L->ready_word] != L->ready_value) {
        return; /* the guest has not published the record yet */
    }
    if (L->playing) {
        /* the run just made was a frame in play */
        L->play_runs++;
        L->play_presented += info->run.presented;
        L->play_repeated += info->run.repeated;
        L->play_no_video += info->run.presented + info->run.repeated == 0;
        L->play_stalls += info->run.tick_step == 0;
        L->play_leaps += info->run.tick_step > 1 ? info->run.tick_step - 1 : 0;
        if (info->run.run_us > L->play_max_run_us) {
            L->play_max_run_us = info->run.run_us;
        }
    }
    if (L->rec[L->screen_word] != L->play_screen) {
        unsigned t = L->menu_frames++;
        if (!L->playing && L->menu_count && t % MENU_EVERY < MENU_HOLD) {
            control->joypad[0] = (uint16_t)(1u << L->menu[(t / MENU_EVERY) % L->menu_count]);
            L->menu_presses += t % MENU_EVERY == 0;
        }
        return;
    }
    {
        float x = word_float(L->rec[L->x_word]);
        if (!L->playing) {
            L->playing = 1;
            L->play_from = info->host_frame;
            L->direction = 1;
            L->x_low = L->x_high = L->x_first = x;
            control->capture = 1; /* capture from the first frame in play */
        }
        if (x < L->x_low) {
            L->x_low = x;
        }
        if (x > L->x_high) {
            L->x_high = x;
        }
        if ((L->direction > 0 && x >= L->x_max) || (L->direction < 0 && x <= L->x_min)) {
            L->direction = -L->direction;
            L->turns++;
        }
        control->joypad[0] = (uint16_t)(1u << lgp_button_id(L->direction > 0 ? "right" : "left"));
        if (L->fire >= 0) {
            control->joypad[0] |= (uint16_t)(1u << L->fire);
        }
        L->play_frames++;
        if (L->play_frames >= L->play_frames_wanted &&
            (!L->capture_frames || info->total->captured_video >= L->capture_frames)) {
            control->stop = 1;
        }
    }
}

static const char *usage =
    "usage: lgp-closed-loop [options] CORE CONTENT SYSTEM_DIR\n"
    "\n"
    "the record (a struct of 32-bit words in guest RAM)\n"
    "  --record ADDR            its guest address, or --symbols FILE with --record-symbol NAME (nm output)\n"
    "  --ram-mask MASK          RAM offset bits (default 0xffffffff)\n"
    "  --ram-base BASE          a guest address where RAM appears; repeatable (SH-4: 0x0c000000 0x8c000000 0xac000000\n"
    "                           with mask 0x01ffffff)\n"
    "  --words N                words to read (default 64)\n"
    "  --ready-word I=V         the record is published once word I equals V\n"
    "  --screen-word I          the word that names the screen; --play-screen V is its value in play\n"
    "  --x-word I               the player's x, a float word; --x-min F and --x-max F the weave (default -3 and 3)\n"
    "  --tick ADDR | --tick-symbol NAME   the guest's frame counter (32 bits); frames before it starts are skipped\n"
    "input\n"
    "  --menu-buttons LIST      pressed in turn until play, one every 30 frames for 4 (default start,b)\n"
    "  --fire NAME              held in play (default none)\n"
    "  --play-frames N          frames to play (default 300)\n"
    "  --strict-pacing          fail unless every frame in play presented one new frame and no dupes\n"
    "capture\n"
    "  --video FILE --audio FILE --capture-frames N --video-size WxH\n"
    "  --check-av-sync          fail unless the audio captured matches the video frames at the core's rate (5%)\n"
    "core\n"
    "  --option KEY=VALUE (repeatable)  --ports N  --max-frames N (default 20000)\n";

static int number(const char *text, uint32_t *v)
{
    char *end;
    unsigned long n;
    if (!text || !*text || *text == '-') {
        return 0;
    }
    errno = 0;
    n = strtoul(text, &end, 0);
    if (*end || errno == ERANGE || n > 0xfffffffful) {
        return 0;
    }
    *v = (uint32_t)n;
    return 1;
}

static int word_value(const char *text, unsigned *index, uint32_t *value)
{
    char buf[64];
    char *eq;
    uint32_t i;
    if (strlen(text) >= sizeof(buf)) {
        return 0;
    }
    strcpy(buf, text);
    eq = strchr(buf, '=');
    if (!eq) {
        return 0;
    }
    *eq = '\0';
    if (!number(buf, &i) || !number(eq + 1, value)) {
        return 0;
    }
    *index = i;
    return 1;
}

static int buttons(const char *text, Loop *L)
{
    char buf[128];
    char *tok;
    if (strlen(text) >= sizeof(buf)) {
        return 0;
    }
    strcpy(buf, text);
    L->menu_count = 0;
    for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        int id = lgp_button_id(tok);
        if (id < 0 || L->menu_count == MAX_MENU) {
            return 0;
        }
        L->menu[L->menu_count++] = id;
    }
    return L->menu_count > 0;
}

int main(int argc, char **argv)
{
    static Loop L;
    LgpConfig config;
    LgpResult result;
    LgpOptions *options = lgp_options_new();
    LgpProbe *probe = lgp_probe_new();
    LgpRamWindow window;
    const char *positional[3], *symbols = NULL, *record_symbol = NULL, *tick_symbol = NULL;
    const char *record_text = NULL, *tick_text = NULL;
    int positionals = 0, i, strict = 0, av_sync = 0, code, pass;
    uint32_t v, address = 0;
    double expected_audio = 0;

    memset(&L, 0, sizeof(L));
    L.words = 64;
    L.screen_word = L.x_word = L.ready_word = 0xffffffffu;
    L.x_min = -3.f;
    L.x_max = 3.f;
    L.fire = -1;
    L.play_frames_wanted = 300;
    buttons("start,b", &L);
    lgp_ram_window_init(&window, 0xffffffffu);
    lgp_config_defaults(&config);

#define VALUE() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "missing value for %s\n", argv[i]), exit(2), ""))
#define BAD(what) (fprintf(stderr, "lgp-closed-loop: bad %s\n%s", what, usage), exit(2))
    for (i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0) {
            fputs(usage, stdout);
            return 0;
        } else if (strcmp(a, "--record") == 0) {
            record_text = VALUE();
        } else if (strcmp(a, "--symbols") == 0) {
            symbols = VALUE();
        } else if (strcmp(a, "--record-symbol") == 0) {
            record_symbol = VALUE();
        } else if (strcmp(a, "--tick") == 0) {
            tick_text = VALUE();
        } else if (strcmp(a, "--tick-symbol") == 0) {
            tick_symbol = VALUE();
        } else if (strcmp(a, "--ram-mask") == 0) {
            if (!number(VALUE(), &v)) {
                BAD("--ram-mask");
            }
            window.mask = v;
        } else if (strcmp(a, "--ram-base") == 0) {
            if (!number(VALUE(), &v) || !lgp_ram_window_add_base(&window, v)) {
                BAD("--ram-base (give --ram-mask first; a base has no bits inside the mask)");
            }
        } else if (strcmp(a, "--words") == 0) {
            if (!number(VALUE(), &v) || v == 0 || v > MAX_WORDS) {
                BAD("--words");
            }
            L.words = v;
        } else if (strcmp(a, "--ready-word") == 0) {
            if (!word_value(VALUE(), &L.ready_word, &L.ready_value)) {
                BAD("--ready-word (want I=V)");
            }
        } else if (strcmp(a, "--screen-word") == 0) {
            if (!number(VALUE(), &v)) {
                BAD("--screen-word");
            }
            L.screen_word = v;
        } else if (strcmp(a, "--play-screen") == 0) {
            if (!number(VALUE(), &L.play_screen)) {
                BAD("--play-screen");
            }
        } else if (strcmp(a, "--x-word") == 0) {
            if (!number(VALUE(), &v)) {
                BAD("--x-word");
            }
            L.x_word = v;
        } else if (strcmp(a, "--x-min") == 0) {
            L.x_min = strtof(VALUE(), NULL);
        } else if (strcmp(a, "--x-max") == 0) {
            L.x_max = strtof(VALUE(), NULL);
        } else if (strcmp(a, "--menu-buttons") == 0) {
            if (!buttons(VALUE(), &L)) {
                BAD("--menu-buttons (want names such as start,b)");
            }
        } else if (strcmp(a, "--fire") == 0) {
            L.fire = lgp_button_id(VALUE());
            if (L.fire < 0) {
                BAD("--fire");
            }
        } else if (strcmp(a, "--play-frames") == 0) {
            if (!number(VALUE(), &L.play_frames_wanted) || !L.play_frames_wanted) {
                BAD("--play-frames");
            }
        } else if (strcmp(a, "--strict-pacing") == 0) {
            strict = 1;
        } else if (strcmp(a, "--check-av-sync") == 0) {
            av_sync = 1;
        } else if (strcmp(a, "--video") == 0) {
            config.video_path = VALUE();
        } else if (strcmp(a, "--audio") == 0) {
            config.audio_path = VALUE();
        } else if (strcmp(a, "--capture-frames") == 0) {
            if (!number(VALUE(), &L.capture_frames)) {
                BAD("--capture-frames");
            }
            config.capture_max_frames = L.capture_frames;
        } else if (strcmp(a, "--video-size") == 0) {
            if (sscanf(VALUE(), "%ux%u", &config.capture_width, &config.capture_height) != 2) {
                BAD("--video-size");
            }
        } else if (strcmp(a, "--option") == 0) {
            if (!lgp_options_parse(options, VALUE())) {
                BAD("--option");
            }
        } else if (strcmp(a, "--ports") == 0) {
            if (!number(VALUE(), &config.ports) || config.ports < 1 || config.ports > LGP_MAX_PORTS) {
                BAD("--ports");
            }
        } else if (strcmp(a, "--max-frames") == 0) {
            if (!number(VALUE(), &config.max_frames)) {
                BAD("--max-frames");
            }
        } else if (a[0] == '-' && a[1] == '-') {
            BAD(a);
        } else if (positionals < 3) {
            positional[positionals++] = a;
        } else {
            BAD("arguments (too many)");
        }
    }
    if (positionals != 3) {
        BAD("arguments (want CORE CONTENT SYSTEM_DIR)");
    }
    if (record_symbol) {
        if (!lgp_nm_lookup(symbols, record_symbol, &address)) {
            fprintf(stderr, "lgp-closed-loop: no %s in %s\n", record_symbol, symbols ? symbols : "(no --symbols)");
            return 2;
        }
    } else if (!record_text || !number(record_text, &address)) {
        BAD("--record (or --symbols and --record-symbol)");
    }
    if (!lgp_ram_offset(&window, address, &L.record)) {
        fprintf(stderr, "lgp-closed-loop: 0x%08x is not in the RAM window\n", (unsigned)address);
        return 2;
    }
    if (L.ready_word >= L.words || L.screen_word >= L.words || L.x_word >= L.words) {
        BAD("words: --ready-word, --screen-word and --x-word are needed, inside --words");
    }
    if (tick_symbol || tick_text) {
        uint32_t tick_offset;
        char text[32];
        if (tick_symbol ? !lgp_nm_lookup(symbols, tick_symbol, &address) : !number(tick_text, &address)) {
            BAD("--tick or --tick-symbol");
        }
        if (!lgp_ram_offset(&window, address, &tick_offset)) {
            fprintf(stderr, "lgp-closed-loop: tick 0x%08x is not in the RAM window\n", (unsigned)address);
            return 2;
        }
        snprintf(text, sizeof(text), "0x%x:4", (unsigned)tick_offset);
        lgp_probe_parse_tick(probe, text);
    }
    printf("RECORD offset=0x%x words=%u\n", (unsigned)L.record, L.words);

    config.core_path = positional[0];
    config.content_path = strcmp(positional[1], "-") == 0 ? NULL : positional[1];
    config.system_dir = positional[2];
    config.options = options;
    config.probe = probe;
    config.frame_hook = hook;
    config.frame_user = &L;
    code = lgp_run(&config, &result);

    if (result.fps > 0) {
        expected_audio = (double)result.counters.captured_video * result.sample_rate / result.fps;
    }
    printf("MENU frames=%u presses=%u\n", L.menu_frames, L.menu_presses);
    printf("PLAY from_frame=%u frames=%u turns=%u x_first=%.3f x_low=%.3f x_high=%.3f\n", L.play_from, L.play_frames,
           L.turns, L.x_first, L.x_low, L.x_high);
    printf("PACING runs=%u presented=%u repeated=%u no_video=%u guest_stalls=%u guest_leaps=%u max_run_us=%u\n",
           L.play_runs, L.play_presented, L.play_repeated, L.play_no_video, L.play_stalls, L.play_leaps,
           L.play_max_run_us);
    printf("TOTAL runs=%u presented=%u repeated=%u no_video=%u guest_stalls=%u guest_leaps=%u audio_frames=%llu\n",
           result.counters.runs, result.counters.presented, result.counters.repeated, result.counters.no_video,
           result.counters.guest_stalls, result.counters.guest_leaps, (unsigned long long)result.counters.audio_frames);
    printf("CAPTURE video_frames=%u size=%ux%u audio_frames=%llu expected_audio=%.0f fps=%.4f sample_rate=%.0f "
           "error=%d\n",
           result.counters.captured_video, result.video_width, result.video_height,
           (unsigned long long)result.counters.captured_audio, expected_audio, result.fps, result.sample_rate,
           result.capture_error);

    pass = code == LGP_OK && !L.record_out_of_range && L.playing && L.play_frames >= L.play_frames_wanted &&
           L.turns >= 1 && L.x_high > L.x_low && result.counters.presented > 0;
    if (config.video_path && L.capture_frames) {
        pass = pass && result.counters.captured_video == L.capture_frames;
    }
    if (config.audio_path) {
        pass = pass && result.counters.captured_audio > 0;
    }
    if (av_sync) {
        double off = (double)result.counters.captured_audio - expected_audio;
        pass = pass && expected_audio > 0 && (off < 0 ? -off : off) <= expected_audio * 0.05;
    }
    if (strict) {
        pass = pass && L.play_repeated == 0 && L.play_no_video == 0 && L.play_presented == L.play_runs;
    }
    printf("CHECK %s exit=%d\n", pass ? "PASS" : "FAIL", code);
    lgp_probe_free(probe);
    lgp_options_free(options);
    return pass ? 0 : 1;
}
