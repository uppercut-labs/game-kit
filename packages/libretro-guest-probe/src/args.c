/* The command line: argv into a setup (config, option table, probe, script). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lgp/lgp.h"

const char *lgp_usage(void)
{
    return "usage: libretro-guest-probe [options] CORE CONTENT SYSTEM_DIR\n"
           "\n"
           "Runs a libretro core with no window and no audio, feeds it pad input, and reads the guest's memory.\n"
           "CORE is the core's shared library, CONTENT the game (or '-' for none), SYSTEM_DIR the core's own system and\n"
           "save folder (give each run a fresh one).\n"
           "\n"
           "input\n"
           "  --replay FILE          play a pad-replay file: slot N feeds port N (frames are the guest tick with --tick,\n"
           "                         else the host frame)\n"
           "  --tap TICK:MASK[:HOLD] press MASK on port 0 at TICK for HOLD ticks (default 15); repeatable, not with --replay\n"
           "  --map NAME=MASK        which bit of the replay's buttons is a libretro joypad button (b y select start up\n"
           "                         down left right a x l r l2 r2 l3 r3); repeatable; needed for --replay and --tap\n"
           "  --map-file FILE        the same, one NAME=MASK per line, # comments\n"
           "  --ports N              ports fed, 1 to 8 (default 1)\n"
           "  --record FILE          write the pad states the core asked for, in the replay format (new file only)\n"
           "core\n"
           "  --option KEY=VALUE     answer a core option; KEY may hold * (so '*serial*=enabled'); repeatable\n"
           "  --gl MAJOR.MINOR       the OpenGL core context made if the core wants one (default 3.2)\n"
           "guest memory\n"
           "  --memory-id N          RETRO_MEMORY_* to read (default 2, system RAM)\n"
           "  --address-mask MASK    applied to every address (default 0xffffffff; 0x00ffffff for mirrored RAM)\n"
           "  --tick ADDR[:SIZE]     the guest's frame counter; frames are skipped while it reads 0\n"
           "  --watch NAME=ADDR[:SIZE]   log changes of a value (SIZE 1, 2 or 4, default 4; add 'be' for big endian: 4be)\n"
           "  --expect NAME=VALUE    the run fails unless the watch equalled VALUE at some point; repeatable\n"
           "  --stop NAME=VALUE[@TICK]   end the run when the watch equals VALUE and the tick is at least TICK\n"
           "run\n"
           "  --max-frames N         bound on host frames (default 20000)\n"
           "  --delay-ms N           sleep when the guest has not started (default 1)\n"
           "  --frame-delay-ms N     sleep after every frame (default 0)\n"
           "  --version, --help\n"
           "\n"
           "exit: 0 ran and every --expect was seen, 1 an --expect was not seen, 2 usage, 3 core, 4 load, 5 record file\n";
}

typedef struct Tap {
    uint32_t tick, mask, hold;
} Tap;

static int number(const char *text, uint32_t *value)
{
    char *end;
    unsigned long v;
    if (!text || !*text || *text == '-' || *text == '+') {
        return 0;
    }
    errno = 0;
    v = strtoul(text, &end, 0);
    if (*end || errno == ERANGE || v > 0xfffffffful) {
        return 0;
    }
    *value = (uint32_t)v;
    return 1;
}

static int bad(FILE *err, const char *what, const char *text)
{
    if (err) {
        fprintf(err, "libretro-guest-probe: %s: %s\n", what, text ? text : "(missing)");
    }
    return LGP_ERR_ARGS;
}

static int add_map(LgpSetup *s, const char *text, FILE *err)
{
    LgpButtonMap entry;
    if (!lgp_map_parse(text, &entry)) {
        return bad(err, "bad --map (want NAME=MASK, e.g. start=0x0008)", text);
    }
    if (s->config.map_count == LGP_MAX_MAP) {
        return bad(err, "too many --map entries", text);
    }
    s->config.map[s->config.map_count++] = entry;
    return LGP_OK;
}

static int read_map_file(LgpSetup *s, const char *path, FILE *err)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int code = LGP_OK;
    if (!f) {
        return bad(err, "cannot open --map-file", path);
    }
    while (code == LGP_OK && fgets(line, sizeof(line), f)) {
        char *p = line, *end;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '#' || *p == '\n' || *p == '\r' || !*p) {
            continue;
        }
        end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
            *--end = '\0';
        }
        code = add_map(s, p, err);
    }
    fclose(f);
    return code;
}

static int tap_order(const void *a, const void *b)
{
    const Tap *x = (const Tap *)a, *y = (const Tap *)b;
    return x->tick < y->tick ? -1 : x->tick > y->tick;
}

/* The taps as a replay on slot 0: connected at rest from tick 0, then each press and release. */
static int taps_to_script(LgpSetup *s, Tap *taps, size_t count, FILE *err)
{
    typedef struct Ev {
        uint32_t tick, mask;
    } Ev;
    Ev *ev = (Ev *)malloc((count * 2 + 1) * sizeof(Ev));
    char *text;
    size_t i, n = 0, used = 0;
    PadrError perr;
    int code = LGP_OK;

    qsort(taps, count, sizeof(Tap), tap_order);
    if (!ev) {
        return bad(err, "out of memory", "--tap");
    }
    ev[n].tick = 0;
    ev[n++].mask = 0;
    for (i = 0; i < count; ++i) {
        ev[n].tick = taps[i].tick;
        ev[n++].mask = taps[i].mask;
        ev[n].tick = taps[i].tick + taps[i].hold;
        ev[n++].mask = 0;
    }
    /* releases can fall after a later press: order everything by tick, presses of the same tick after releases */
    for (i = 1; i < n; ++i) {
        Ev v = ev[i];
        size_t k = i;
        while (k > 0 && (ev[k - 1].tick > v.tick || (ev[k - 1].tick == v.tick && ev[k - 1].mask != 0 && v.mask == 0))) {
            ev[k] = ev[k - 1];
            --k;
        }
        ev[k] = v;
    }
    text = (char *)malloc(n * 40 + 1);
    if (!text) {
        free(ev);
        return bad(err, "out of memory", "--tap");
    }
    for (i = 0; i < n; ++i) {
        used += (size_t)sprintf(text + used, "%lu 0 %04lx 128 128 1\n", (unsigned long)ev[i].tick,
                                (unsigned long)ev[i].mask);
    }
    s->script = padr_load_text(text, used, NULL, &perr);
    if (!s->script) {
        code = bad(err, "the --tap events make no valid replay", padr_status_text(perr.status));
    }
    free(text);
    free(ev);
    return code;
}

int lgp_args_parse(int argc, char **argv, LgpSetup *s, FILE *err)
{
    Tap *taps = NULL;
    size_t tap_count = 0, tap_capacity = 0;
    const char *positional[3];
    int positionals = 0, i, code = LGP_OK;
    const char *replay = NULL;
    uint32_t v;

    memset(s, 0, sizeof(*s));
    lgp_config_defaults(&s->config);
    s->options = lgp_options_new();
    s->probe = lgp_probe_new();
    if (!s->options || !s->probe) {
        return bad(err, "out of memory", "setup");
    }

#define NEED(flag)                                      \
    do {                                                \
        if (i + 1 >= argc) {                            \
            code = bad(err, "missing value for", flag); \
            goto done;                                  \
        }                                               \
    } while (0)
#define NUMBER(flag, target)                                          \
    do {                                                              \
        NEED(flag);                                                   \
        if (!number(argv[++i], &v)) {                                 \
            code = bad(err, "not a number for " flag, argv[i]);       \
            goto done;                                                \
        }                                                             \
        (target) = v;                                                 \
    } while (0)

    for (i = 1; i < argc && code == LGP_OK; ++i) {
        const char *a = argv[i];
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            fputs(lgp_usage(), stdout);
            code = LGP_HELP;
        } else if (strcmp(a, "--version") == 0) {
            printf("libretro-guest-probe %s\n", LGP_VERSION);
            code = LGP_HELP;
        } else if (strcmp(a, "--replay") == 0) {
            NEED(a);
            replay = argv[++i];
        } else if (strcmp(a, "--record") == 0) {
            NEED(a);
            s->config.record_path = argv[++i];
        } else if (strcmp(a, "--tap") == 0) {
            uint32_t tick, mask, hold = 15;
            char *c1, *c2;
            char buf[64];
            NEED(a);
            ++i;
            if (strlen(argv[i]) >= sizeof(buf)) {
                code = bad(err, "bad --tap", argv[i]);
                break;
            }
            strcpy(buf, argv[i]);
            c1 = strchr(buf, ':');
            if (!c1) {
                code = bad(err, "bad --tap (want TICK:MASK[:HOLD])", argv[i]);
                break;
            }
            *c1++ = '\0';
            c2 = strchr(c1, ':');
            if (c2) {
                *c2++ = '\0';
            }
            if (!number(buf, &tick) || !number(c1, &mask) || mask > 0xffffu || (c2 && (!number(c2, &hold) || hold == 0))) {
                code = bad(err, "bad --tap (want TICK:MASK[:HOLD])", argv[i]);
                break;
            }
            if (tap_count == tap_capacity) {
                size_t grown = tap_capacity ? tap_capacity * 2 : 8;
                Tap *bigger = (Tap *)realloc(taps, grown * sizeof(Tap));
                if (!bigger) {
                    code = bad(err, "out of memory", "--tap");
                    break;
                }
                taps = bigger;
                tap_capacity = grown;
            }
            taps[tap_count].tick = tick;
            taps[tap_count].mask = mask;
            taps[tap_count].hold = hold;
            tap_count++;
        } else if (strcmp(a, "--map") == 0) {
            NEED(a);
            code = add_map(s, argv[++i], err);
        } else if (strcmp(a, "--map-file") == 0) {
            NEED(a);
            code = read_map_file(s, argv[++i], err);
        } else if (strcmp(a, "--ports") == 0) {
            NUMBER("--ports", s->config.ports);
            if (s->config.ports < 1 || s->config.ports > LGP_MAX_PORTS) {
                code = bad(err, "--ports is 1 to 8", argv[i]);
            }
        } else if (strcmp(a, "--option") == 0) {
            NEED(a);
            if (!lgp_options_parse(s->options, argv[++i])) {
                code = bad(err, "bad --option (want KEY=VALUE)", argv[i]);
            }
        } else if (strcmp(a, "--gl") == 0) {
            unsigned major, minor;
            NEED(a);
            if (sscanf(argv[++i], "%u.%u", &major, &minor) != 2 || major < 2 || major > 4 || minor > 9) {
                code = bad(err, "bad --gl (want MAJOR.MINOR, e.g. 3.2)", argv[i]);
            } else {
                s->config.gl_major = major;
                s->config.gl_minor = minor;
            }
        } else if (strcmp(a, "--memory-id") == 0) {
            NUMBER("--memory-id", s->config.memory_id);
        } else if (strcmp(a, "--address-mask") == 0) {
            NUMBER("--address-mask", v);
            lgp_probe_set_mask(s->probe, v);
        } else if (strcmp(a, "--tick") == 0) {
            NEED(a);
            if (!lgp_probe_parse_tick(s->probe, argv[++i])) {
                code = bad(err, "bad --tick (want ADDR[:SIZE])", argv[i]);
            }
        } else if (strcmp(a, "--watch") == 0) {
            NEED(a);
            if (!lgp_probe_parse_watch(s->probe, argv[++i])) {
                code = bad(err, "bad --watch (want NAME=ADDR[:SIZE], names up to 31 characters, no repeats)", argv[i]);
            }
        } else if (strcmp(a, "--expect") == 0) {
            NEED(a);
            if (!lgp_probe_parse_expect(s->probe, argv[++i])) {
                code = bad(err, "bad --expect (want NAME=VALUE for a name given to --watch first)", argv[i]);
            }
        } else if (strcmp(a, "--stop") == 0) {
            NEED(a);
            if (!lgp_probe_parse_stop(s->probe, argv[++i])) {
                code = bad(err, "bad --stop (want NAME=VALUE[@TICK] for a name given to --watch first)", argv[i]);
            }
        } else if (strcmp(a, "--max-frames") == 0) {
            NUMBER("--max-frames", s->config.max_frames);
        } else if (strcmp(a, "--delay-ms") == 0) {
            NUMBER("--delay-ms", s->config.not_ready_delay_ms);
        } else if (strcmp(a, "--frame-delay-ms") == 0) {
            NUMBER("--frame-delay-ms", s->config.frame_delay_ms);
        } else if (a[0] == '-' && a[1] == '-') {
            code = bad(err, "unknown option", a);
        } else if (positionals < 3) {
            positional[positionals++] = a;
        } else {
            code = bad(err, "too many arguments", a);
        }
    }
    if (code != LGP_OK) {
        goto done;
    }
    if (positionals != 3) {
        code = bad(err, "want CORE CONTENT SYSTEM_DIR (try --help)", NULL);
        goto done;
    }
    s->config.core_path = positional[0];
    s->config.content_path = strcmp(positional[1], "-") == 0 ? NULL : positional[1];
    s->config.system_dir = positional[2];
    if (replay && tap_count) {
        code = bad(err, "--replay and --tap cannot be used together", replay);
        goto done;
    }
    if (replay) {
        PadrOptions opt = {LGP_MAX_PORTS, 0};
        PadrError perr;
        s->script = padr_load_file(replay, &opt, &perr);
        if (!s->script) {
            if (err) {
                fprintf(err, "libretro-guest-probe: --replay %s: %s (line %u)\n", replay, padr_status_text(perr.status),
                        perr.line);
            }
            code = LGP_ERR_ARGS;
            goto done;
        }
    } else if (tap_count) {
        code = taps_to_script(s, taps, tap_count, err);
        if (code != LGP_OK) {
            goto done;
        }
    }
    if (s->script && s->config.map_count == 0) {
        code = bad(err, "a replay or tap needs a button map: give --map or --map-file", "");
        goto done;
    }
    s->config.options = s->options;
    s->config.probe = s->probe;
    s->config.script = s->script;
done:
    free(taps);
    return code;
}

void lgp_setup_free(LgpSetup *s)
{
    if (!s) {
        return;
    }
    lgp_options_free(s->options);
    lgp_probe_free(s->probe);
    padr_free(s->script);
    memset(s, 0, sizeof(*s));
}
