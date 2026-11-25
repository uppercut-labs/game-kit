/* The host end to end, against a fake libretro core (fake_core.c): scripted input reaches the core, the core's option
 * query is answered, the guest's memory is probed through a masked address, the not-started guard skips frames, a stop
 * rule ends the run, the pads are recorded in the replay format, and a hardware-render request is refused cleanly where no
 * display exists. No real emulator, no GPU, no game: see the README for what this does not cover.
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
    test_scripted_run();
    test_expectation_failure_and_frame_bound();
    test_no_probe_and_host_frames();
    test_memory_unavailable();
    test_hardware_render_is_refused_without_a_display();
    test_core_that_cannot_load();
    printf("host_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
