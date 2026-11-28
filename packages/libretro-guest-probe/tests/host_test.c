/* The host end to end, against a fake libretro core (fake_core.c): scripted input reaches the core, the core's option
 * query is answered, the guest's memory is probed through a masked address, the not-started guard skips frames, a stop
 * rule ends the run, the pads are recorded in the replay format, and a hardware-render request is refused cleanly where no
 * display exists. Then the frame hook drives two ports from guest memory, the video and audio are counted and captured,
 * and the guest's stalls and leaps are counted. No real emulator, no GPU, no game: see the README for what this does not
 * cover.
 *
 *   host_test <fake core library> <scratch dir>
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "libretro.h"
#include "lgp/lgp.h"

#ifdef _WIN32
#define SETENV(name, value) _putenv_s(name, value)
#define UNSETENV(name) _putenv_s(name, "")
#else
#define SETENV(name, value) setenv(name, value, 1)
#define UNSETENV(name) unsetenv(name)
#endif

static const char *core_path, *scratch;

static char *path_of(const char *name)
{
    static char paths[4][1200];
    static unsigned next;
    char *p = paths[next++ % 4];
    snprintf(p, 1200, "%s/%s", scratch, name);
    return p;
}

static FILE *tmp(void)
{
    FILE *f = tmpfile();
    if (!f) {
        exit(2);
    }
    return f;
}

static int log_has(FILE *f, const char *text)
{
    char buf[16384];
    size_t n;
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    return strstr(buf, text) != NULL;
}

/* A setup from a command line, like main() builds it, with the fake core in front. */
static int setup_from(LgpSetup *s, FILE *log, int extra, char **extra_args)
{
    char *argv[48];
    int argc = 0, i;
    argv[argc++] = "lgp";
    for (i = 0; i < extra; ++i) {
        argv[argc++] = extra_args[i];
    }
    argv[argc++] = (char *)core_path;
    argv[argc++] = "-";
    argv[argc++] = path_of("system");
    {
        int code = lgp_args_parse(argc, argv, s, stderr);
        s->config.log = log;
        return code;
    }
}

static void test_scripted_run(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    PadrScript *rec;
    PadrSample out[8];
    char *extra[] = {"--option", "*serial*=enabled", "--map", "start=0x0008", "--map", "b=0x4000", "--tap", "20:0x0008:5",
                     "--tick", "0x8c000100:4", "--address-mask", "0x00ffffff", "--watch", "state=0x8c000104",
                     "--watch", "pad=0x8c000108", "--watch", "opt=0x8c000120", "--watch", "optbyte=0x8c000124:1",
                     "--watch", "unmatched=0x8c000128", "--watch", "hwrefused=0x8c000130", "--watch", "port0dev=0x8c000140:1",
                     "--expect", "state=3", "--expect", "pad=8", "--expect", "opt=1", "--expect", "optbyte=101",
                     "--expect", "unmatched=0", "--expect", "port0dev=2", "--stop", "state=3@30",
                     "--record", NULL, "--max-frames", "500"};
    int n = 0;
    int code;

    while (extra[n]) {
        ++n;
    }
    remove(path_of("scripted.input"));
    extra[n] = path_of("scripted.input");
    n += 3; /* the path, --max-frames, 500 */
    SETENV("LGP_FAKE_HW", "");
    UNSETENV("LGP_FAKE_HW");

    code = setup_from(&s, log, n, extra);
    CHECK(code == LGP_OK, "the command line parses (%d)", code);
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_OK, "the run passes (%d)", code);
    CHECK(r.stopped, "the stop rule ended the run");
    CHECK(r.not_ready_frames == 5, "the five frames before the guest started were skipped, got %u", r.not_ready_frames);
    CHECK(r.frames_run < 100 && r.last_tick >= 30, "it stopped early: %u frames, tick %u", r.frames_run, r.last_tick);
    CHECK(r.expectations_met, "every expectation was seen");
    CHECK(!r.hw_render_requested, "no hardware render was asked for");
    CHECK(log_has(log, "CORE lgp-fake-core 0 need_fullpath=1"), "the core was identified");
    CHECK(log_has(log, "OPTION fake_serial_mode=enabled"), "the option answer was logged once");
    CHECK(log_has(log, "EXPECT state=0x3 PASS") && log_has(log, "PROBE PASS"), "the verdict lines");
    CHECK(log_has(log, "WATCH state host_frame=") && log_has(log, "value=0x3"), "state changes were logged");
    lgp_setup_free(&s);

    /* the recorded pads replay: the press at tick 20 for 5 ticks, as the core polled it */
    rec = padr_load_file(path_of("scripted.input"), NULL, NULL);
    CHECK(rec != NULL, "the record file loads with the strict reader");
    if (rec) {
        padr_sample(rec, 19, out, 8);
        CHECK(out[0].connected == PADR_FRESH && out[0].buttons == 0, "recorded at rest before the press");
        padr_sample(rec, 21, out, 8);
        CHECK(out[0].buttons == 0x0008, "recorded pressed");
        padr_sample(rec, 26, out, 8);
        CHECK(out[0].buttons == 0, "recorded released");
        CHECK(padr_event_count(rec) <= 6, "only changes are recorded: %u events", padr_event_count(rec));
        padr_free(rec);
    }

    /* an existing record file is never overwritten */
    code = setup_from(&s, log, n, extra);
    code = code == LGP_OK ? lgp_run(&s.config, &r) : code;
    CHECK(code == LGP_ERR_IO, "the record file exists: %d", code);
    lgp_setup_free(&s);
}

static void test_expectation_failure_and_frame_bound(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--tick", "0x100", "--watch", "state=0x104", "--expect", "state=3", "--max-frames", "40"};
    int code = setup_from(&s, log, 8, extra);

    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_EXPECT_FAILED, "no button is ever pressed, so state 3 is never seen (%d)", code);
    CHECK(r.frames_run == 40 && !r.stopped && !r.expectations_met, "the frame bound ended the run: %u frames", r.frames_run);
    CHECK(r.not_ready_frames == 5, "five frames skipped");
    CHECK(log_has(log, "EXPECT state=0x3 FAIL") && log_has(log, "PROBE FAIL"), "and it says so");
    lgp_setup_free(&s);
}

static void test_no_probe_and_host_frames(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--map", "start=0x0008", "--tap", "3:0x0008:2", "--ports", "2", "--max-frames", "10"};
    int code = setup_from(&s, log, 8, extra);

    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_OK && r.frames_run == 10 && r.not_ready_frames == 0 && r.expectations_met,
          "with no memory probe every frame counts (%d, %u frames)", code, r.frames_run);
    lgp_setup_free(&s);
}

static void test_memory_unavailable(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    /* RETRO_MEMORY_SAVE_RAM is not there in the fake core: the guest never reads as started */
    char *extra[] = {"--memory-id", "0", "--tick", "0x100", "--delay-ms", "0", "--max-frames", "12"};
    int code = setup_from(&s, log, 8, extra);

    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_OK && r.frames_run == 12 && r.not_ready_frames == 12, "every frame is skipped without memory (%u)", r.not_ready_frames);
    CHECK(log_has(log, "GUEST MEMORY unavailable"), "and it is said once");
    lgp_setup_free(&s);
}

static void test_hardware_render_is_refused_without_a_display(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--tick", "0x100", "--watch", "refused=0x130", "--expect", "refused=1", "--max-frames", "8"};
    int code;

    SETENV("LGP_FAKE_HW", "1");
    SETENV("SDL_VIDEODRIVER", "dummy"); /* a video driver with no OpenGL: the context cannot be made */
    code = setup_from(&s, log, 8, extra);
    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    UNSETENV("LGP_FAKE_HW");
    UNSETENV("SDL_VIDEODRIVER");
    CHECK(r.hw_render_requested, "the core asked for a hardware context");
    CHECK(code == LGP_OK && r.expectations_met, "the core was told no, and noticed (%d)", code);
    CHECK(log_has(log, "HW_CONTEXT requested=") && log_has(log, "HW_CONTEXT refused"), "the refusal is logged");
    lgp_setup_free(&s);
}


static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static uint32_t rd32(const uint8_t *m, unsigned at)
{
    return (uint32_t)m[at] | ((uint32_t)m[at + 1] << 8) | ((uint32_t)m[at + 2] << 16) | ((uint32_t)m[at + 3] << 24);
}

/* the fake core's video for run r (from 1): none, a dupe, or a new frame */
static unsigned fake_presented(unsigned runs)
{
    unsigned r, n = 0;
    for (r = 1; r <= runs; ++r) {
        n += r % 4u == 1u || r % 4u == 2u;
    }
    return n;
}

typedef struct HookState {
    unsigned calls, first_ready, bad_pad0, bad_pad1, bad_trigger, bad_total, bad_run, stop_at;
    uint16_t sent0, sent1;
    int have_sent;
    int capture_off;
} HookState;

static void hook(void *user, const LgpFrameInfo *info, LgpFrameControl *control)
{
    HookState *st = (HookState *)user;
    unsigned r = info->host_frame + 1u;
    st->calls++;
    if (info->total->runs != r) {
        st->bad_total++;
    }
    if (info->run.presented != (unsigned)(r % 4u == 1u || r % 4u == 2u) || info->run.repeated != (unsigned)(r % 4u == 3u) ||
        info->run.audio_frames != 8u + (unsigned)(r % 2u == 0u) || info->ports != 2u) {
        st->bad_run++;
    }
    if (info->ready && !st->first_ready) {
        st->first_ready = r;
    }
    if (st->have_sent && info->memory) {
        /* the core read what the hook decided after the run before this one */
        if (rd32(info->memory, 0x108) != st->sent0) {
            st->bad_pad0++;
        }
        if (rd32(info->memory, 0x150) != st->sent1) {
            st->bad_pad1++;
        }
        if (rd32(info->memory, 0x154) != ((st->sent0 & (1u << RETRO_DEVICE_ID_JOYPAD_R2)) ? 0x7fffu + 0x8000u : 0x8000u)) {
            st->bad_trigger++;
        }
    }
    control->joypad[0] = (uint16_t)(info->host_frame % 2u ? (1u << RETRO_DEVICE_ID_JOYPAD_R2) | (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)
                                                          : (1u << RETRO_DEVICE_ID_JOYPAD_START));
    control->joypad[1] = (uint16_t)(0x0030u | (info->host_frame & 0xfu));
    st->sent0 = control->joypad[0];
    st->sent1 = control->joypad[1];
    st->have_sent = 1;
    if (st->capture_off) {
        control->capture = 0;
    }
    if (info->host_frame + 1u >= st->stop_at) {
        control->stop = 1;
    }
}

static void test_frame_hook_and_capture(void)
{
    LgpConfig c;
    LgpResult r;
    LgpProbe *probe = lgp_probe_new();
    HookState st;
    FILE *log = tmp();
    uint8_t frames[6 * 8 * 4 * 3];
    int16_t audio[4];
    FILE *f;
    int code;

    memset(&st, 0, sizeof(st));
    st.stop_at = 40;
    lgp_probe_parse_tick(probe, "0x100");
    lgp_config_defaults(&c);
    c.core_path = core_path;
    c.system_dir = path_of("system");
    c.log = log;
    c.probe = probe;
    c.ports = 2;
    c.frame_hook = hook;
    c.frame_user = &st;
    c.video_path = path_of("hook.rgb");
    c.audio_path = path_of("hook.raw");
    c.capture_max_frames = 6;
    code = lgp_run(&c, &r);
    CHECK(code == LGP_OK, "the hooked run passes (%d)", code);
    CHECK(r.stopped && r.frames_run == 40 && st.calls == 40, "the hook ended the run at 40 frames: %u", r.frames_run);
    CHECK(st.first_ready == 6 && r.not_ready_frames == 5, "the guest was ready from run 6 (%u)", st.first_ready);
    CHECK(!st.bad_pad0 && !st.bad_pad1, "both ports read what the hook decided (%u %u wrong)", st.bad_pad0, st.bad_pad1);
    CHECK(!st.bad_trigger, "R2 read as a full analog trigger (%u wrong)", st.bad_trigger);
    CHECK(!st.bad_total && !st.bad_run, "the per-run and running counts were right in the hook (%u %u)", st.bad_total,
          st.bad_run);
    CHECK(r.counters.runs == 40 && r.counters.presented == fake_presented(40) && r.counters.repeated == 10 &&
              r.counters.no_video == 10,
          "frames: %u runs, %u presented, %u repeated, %u with no video", r.counters.runs, r.counters.presented,
          r.counters.repeated, r.counters.no_video);
    CHECK(r.counters.audio_frames == 40u * 8u + 20u, "audio frames %llu", (unsigned long long)r.counters.audio_frames);
    CHECK(r.counters.guest_stalls == 0 && r.counters.guest_leaps == 0, "the tick moved by one every run");
    CHECK(r.fps == 60.0 && r.sample_rate == 32000.0, "the AV info was read");
    /* capture: armed at the first ready frame (run 6), so from run 7: 7 a dupe before any frame (skipped), 9 and 10 new,
     * 11 a dupe of 10, 13 and 14 new, 15 a dupe (the sixth), and the audio to the end of run 15 */
    CHECK(r.counters.captured_video == 6 && r.video_width == 8 && r.video_height == 4 && !r.capture_error,
          "six 8x4 frames captured (%u)", r.counters.captured_video);
    CHECK(r.counters.captured_audio == 9u * 8u + 4u, "audio from runs 7 to 15: %llu",
          (unsigned long long)r.counters.captured_audio);
    CHECK(file_size(path_of("hook.rgb")) == (long)sizeof(frames) && file_size(path_of("hook.raw")) == (9 * 8 + 4) * 4,
          "the files hold exactly that");
    f = fopen(path_of("hook.rgb"), "rb");
    if (f && fread(frames, sizeof(frames), 1, f) == 1) {
        const uint8_t *p = frames + (2 * 8 + 1) * 3; /* frame 0, x 1, y 2 */
        CHECK(frames[0] == 9 && frames[1] == 0 && frames[2] == 0 && p[0] == 9 && p[1] == 32 && p[2] == 8,
              "the first frame is run 9's, top-down, XRGB8888 with its pitch: %u %u %u", p[0], p[1], p[2]);
        CHECK(frames[96] == 10 && frames[192] == 10 && frames[288] == 13 && frames[480] == 14,
              "a dupe repeats the frame before it");
    } else {
        CHECK(0, "the video file reads back");
    }
    if (f) {
        fclose(f);
    }
    f = fopen(path_of("hook.raw"), "rb");
    CHECK(f && fread(audio, sizeof(audio), 1, f) == 1 && audio[0] == 7 && audio[1] == -7 && audio[2] == 7,
          "the audio starts with run 7's samples");
    if (f) {
        fclose(f);
    }
    CHECK(log_has(log, "FRAMES runs=40 presented=20 repeated=10 no_video=10") &&
              log_has(log, "CAPTURE video_frames=6 size=8x4 audio_frames=76 error=0") && log_has(log, "AV core fps=60"),
          "the log says so");

    /* the hook can keep the capture off */
    memset(&st, 0, sizeof(st));
    st.stop_at = 20;
    st.capture_off = 1;
    lgp_probe_free(probe);
    probe = lgp_probe_new();
    lgp_probe_parse_tick(probe, "0x100");
    c.probe = probe;
    remove(path_of("hook.rgb"));
    code = lgp_run(&c, &r);
    CHECK(code == LGP_OK && r.counters.captured_video == 0 && r.counters.captured_audio == 0 &&
              file_size(path_of("hook.rgb")) == 0,
          "nothing captured when the hook says no");
    lgp_probe_free(probe);
}

static void test_guest_stalls_and_leaps(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--tick", "0x100", "--max-frames", "40"};
    int code;

    SETENV("LGP_FAKE_PACE", "1");
    code = setup_from(&s, log, 4, extra);
    code = code == LGP_OK ? lgp_run(&s.config, &r) : code;
    UNSETENV("LGP_FAKE_PACE");
    CHECK(code == LGP_OK && r.counters.guest_stalls == 2 && r.counters.guest_leaps == 2,
          "two runs where the tick stood still, two ticks skipped (%u, %u)", r.counters.guest_stalls,
          r.counters.guest_leaps);
    CHECK(r.counters.max_run_us < 1000000u, "the longest run was timed");
    lgp_setup_free(&s);
}

static void test_capture_from_the_command_line(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--tick", "0x100", "--video", NULL, "--audio", NULL, "--capture-frames", "3", "--max-frames", "30"};
    int code;

    extra[3] = path_of("cli.rgb");
    extra[5] = path_of("cli.raw");
    code = setup_from(&s, log, 10, extra);
    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_OK && r.counters.captured_video == 3 && file_size(path_of("cli.rgb")) == 3 * 8 * 4 * 3,
          "three frames from the command line (%d, %u)", code, r.counters.captured_video);
    CHECK(log_has(log, "CAPTURE video_frames=3 size=8x4"), "and the log line");
    lgp_setup_free(&s);
}

static void test_capture_errors(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--tick", "0x100", "--video", NULL, "--video-size", "16x4", "--max-frames", "30"};
    char *bad_dir[] = {"--video", NULL, "--max-frames", "5"};
    int code;

    extra[3] = path_of("wrong-size.rgb");
    code = setup_from(&s, log, 8, extra);
    CHECK(code == LGP_OK, "parse");
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_ERR_IO && r.capture_error && r.frames_run < 30, "an 8x4 frame into a 16x4 capture (%d)", code);
    CHECK(log_has(log, "CAPTURE error: a 8x4 frame, the capture is 16x4"), "is said");
    lgp_setup_free(&s);

    bad_dir[1] = path_of("no-such-folder/out.rgb");
    code = setup_from(&s, log, 4, bad_dir);
    code = code == LGP_OK ? lgp_run(&s.config, &r) : code;
    CHECK(code == LGP_ERR_IO && r.capture_error, "an output that cannot be opened (%d)", code);
    lgp_setup_free(&s);
}

/* With a replay, the hook is handed the replay's buttons as libretro bits; leaving them makes the run a plain replay. */
static uint16_t seen_at_21;
static void passthrough(void *user, const LgpFrameInfo *info, LgpFrameControl *control)
{
    (void)user;
    if (info->ready && info->tick == 21u) {
        seen_at_21 = control->joypad[0];
    }
}

static void test_hook_is_handed_the_replay(void)
{
    LgpSetup s;
    LgpResult r;
    FILE *log = tmp();
    char *extra[] = {"--map", "start=0x0008", "--tap", "21:0x0008:5", "--tick", "0x8c000100", "--address-mask",
                     "0x00ffffff", "--watch", "pad=0x8c000108", "--expect", "pad=8", "--max-frames", "60"};
    int code = setup_from(&s, log, 14, extra);
    CHECK(code == LGP_OK, "parse");
    s.config.frame_hook = passthrough;
    seen_at_21 = 0;
    code = lgp_run(&s.config, &r);
    CHECK(code == LGP_OK && seen_at_21 == (1u << RETRO_DEVICE_ID_JOYPAD_START),
          "the press at tick 21 was offered as Start (%d, 0x%x)", code, seen_at_21);
    lgp_setup_free(&s);
}

static void test_core_that_cannot_load(void)
{
    LgpConfig c;
    LgpResult r;
    FILE *log = tmp();
    lgp_config_defaults(&c);
    c.core_path = path_of("no-such-core.so");
    c.system_dir = path_of("system");
    c.log = log;
    CHECK(lgp_run(&c, &r) == LGP_ERR_CORE, "a missing core");
    c.core_path = NULL;
    CHECK(lgp_run(&c, &r) == LGP_ERR_ARGS, "no core path");
    c.core_path = core_path;
    c.ports = 99;
    CHECK(lgp_run(&c, &r) == LGP_ERR_ARGS, "too many ports");
    CHECK(lgp_run(NULL, &r) == LGP_ERR_ARGS, "no config");
    /* a file that is not a shared library */
    c.ports = 1;
    c.core_path = path_of("not-a-core.txt");
    {
        FILE *f = fopen(c.core_path, "wb");
        if (!f || fputs("this is not a library", f) == EOF || fclose(f) != 0) {
            exit(2);
        }
    }
    CHECK(lgp_run(&c, &r) == LGP_ERR_CORE, "a file that is not a library");
    fclose(log);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <fake core> <scratch dir>\n", argv[0]);
        return 2;
    }
    core_path = argv[1];
    scratch = argv[2];
    UNSETENV("LGP_FAKE_HW");
    UNSETENV("LGP_FAKE_PACE");
    test_scripted_run();
    test_expectation_failure_and_frame_bound();
    test_no_probe_and_host_frames();
    test_memory_unavailable();
    test_hardware_render_is_refused_without_a_display();
    test_frame_hook_and_capture();
    test_guest_stalls_and_leaps();
    test_capture_from_the_command_line();
    test_capture_errors();
    test_hook_is_handed_the_replay();
    test_core_that_cannot_load();
    printf("host_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
