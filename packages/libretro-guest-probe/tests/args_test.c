/* The command line parser: every option into the setup, and every refusal. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "lgp/lgp.h"

static char scratch[1024];

static const char *path_of(const char *name)
{
    static char path[1200];
    snprintf(path, sizeof(path), "%s/%s", scratch, name);
    return path;
}

static void write_text(const char *name, const char *text)
{
    FILE *f = fopen(path_of(name), "wb");
    if (!f || fputs(text, f) == EOF || fclose(f) != 0) {
        exit(2);
    }
}

static int parse(LgpSetup *s, FILE *err, int argc, char **argv)
{
    return lgp_args_parse(argc, argv, s, err);
}


static FILE *tmp(void)
{
    FILE *f = tmpfile();
    if (!f) {
        exit(2);
    }
    return f;
}

static int err_has(FILE *f, const char *text)
{
    char buf[4096];
    size_t n;
    rewind(f);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    return strstr(buf, text) != NULL;
}

static void test_everything(void)
{
    LgpSetup s;
    FILE *err = tmp();
    int code;
    PadrSample out[8];
    char *argv[] = {"lgp", "--option", "*serial*=enabled", "--option", "other=1", "--map", "start=0x0008", "--map", "b=0x4000",
                    "--tick", "0x8c000100:4", "--address-mask", "0x00ffffff", "--watch", "screen=0x8c000104",
                    "--expect", "screen=3", "--stop", "screen=3@450", "--max-frames", "3000", "--delay-ms", "2",
                    "--frame-delay-ms", "1", "--ports", "2", "--gl", "4.1", "--memory-id", "2", "--record", "out.input",
                    "--tap", "180:0x8", "--tap", "260:0x4000:20", "core.so", "game.elf", "system"};
    code = parse(&s, err, (int)(sizeof(argv) / sizeof(argv[0])), argv);
    CHECK(code == LGP_OK, "a full command line parses (%d)", code);
    CHECK(s.config.core_path && strcmp(s.config.core_path, "core.so") == 0 && strcmp(s.config.content_path, "game.elf") == 0 &&
              strcmp(s.config.system_dir, "system") == 0,
          "the three positionals");
    CHECK(s.config.map_count == 2 && s.config.map[0].retro_id == 3 && s.config.map[0].mask == 8 && s.config.map[1].mask == 0x4000,
          "the map");
    CHECK(s.config.max_frames == 3000 && s.config.not_ready_delay_ms == 2 && s.config.frame_delay_ms == 1 && s.config.ports == 2 &&
              s.config.gl_major == 4 && s.config.gl_minor == 1 && s.config.memory_id == 2 &&
              strcmp(s.config.record_path, "out.input") == 0,
          "the numbers");
    CHECK(s.config.options == s.options && lgp_options_count(s.options) == 2, "the options");
    CHECK(s.config.probe == s.probe && lgp_probe_has_tick(s.probe), "the probe");
    CHECK(s.config.script == s.script && s.script != NULL, "the taps became a script");
    if (s.script) {
        padr_sample(s.script, 0, out, 8);
        CHECK(out[0].connected == PADR_FRESH && out[0].buttons == 0, "connected at rest from tick 0");
        padr_sample(s.script, 180, out, 8);
        CHECK(out[0].buttons == 8, "the first tap at 180");
        padr_sample(s.script, 195, out, 8);
        CHECK(out[0].buttons == 0, "released after the default 15");
        padr_sample(s.script, 270, out, 8);
        CHECK(out[0].buttons == 0x4000, "the second tap");
        padr_sample(s.script, 280, out, 8);
        CHECK(out[0].buttons == 0, "released after 20");
    }
    lgp_setup_free(&s);
    fclose(err);
}

static void test_defaults_and_content_dash(void)
{
    LgpSetup s;
    char *argv[] = {"lgp", "core.so", "-", "system"};
    CHECK(parse(&s, NULL, 4, argv) == LGP_OK, "the minimum");
    CHECK(s.config.content_path == NULL, "'-' is no content");
    CHECK(s.config.ports == 1 && s.config.max_frames == 20000 && s.config.not_ready_delay_ms == 1 && s.config.frame_delay_ms == 0 &&
              s.config.gl_major == 3 && s.config.gl_minor == 2 && s.config.memory_id == 2 && s.config.script == NULL &&
              s.config.record_path == NULL,
          "defaults");
    lgp_setup_free(&s);
    lgp_setup_free(&s); /* twice is fine */
}

static void test_replay_and_map_file(void)
{
    LgpSetup s;
    FILE *err = tmp();
    char *argv[] = {"lgp", "--replay", NULL, "--map-file", NULL, "core.so", "c", "sys"};
    char replay[1200], map[1200];

    write_text("r.input", "0 0 0000 128 128 1\n5 0 0008 128 128 1\n");
    write_text("m.txt", "# the port's bits\nstart=0x0008\n\n  b = bad\nx=0x1\n");
    snprintf(replay, sizeof(replay), "%s", path_of("r.input"));
    snprintf(map, sizeof(map), "%s", path_of("m.txt"));
    argv[2] = replay;
    argv[4] = map;
    CHECK(parse(&s, err, 8, argv) == LGP_ERR_ARGS && err_has(err, "bad --map"), "a bad line in the map file is refused");
    lgp_setup_free(&s);

    write_text("m.txt", "# the port's bits\nstart=0x0008\n\n   x=0x0001   \r\n");
    CHECK(parse(&s, err, 8, argv) == LGP_OK, "the map file reads (comments, blanks, spaces, CRLF)");
    CHECK(s.config.map_count == 2 && s.config.script && padr_event_count(s.script) == 2, "map and replay");
    lgp_setup_free(&s);

    write_text("r.input", "0 0 0000 128 128 1\nnot a line\n");
    CHECK(parse(&s, err, 8, argv) == LGP_ERR_ARGS && err_has(err, "line 2"), "a bad replay is refused whole, with its line");
    lgp_setup_free(&s);
    fclose(err);
}

static void test_refusals(void)
{
    static const struct {
        const char *label;
        const char *args[12];
    } bad[] = {
        {"no arguments", {"lgp"}},
        {"two positionals", {"lgp", "core.so", "game"}},
        {"four positionals", {"lgp", "a", "b", "c", "d"}},
        {"unknown option", {"lgp", "--nope", "a", "b", "c"}},
        {"missing value", {"lgp", "a", "b", "c", "--ports"}},
        {"ports 0", {"lgp", "--ports", "0", "a", "b", "c"}},
        {"ports 9", {"lgp", "--ports", "9", "a", "b", "c"}},
        {"not a number", {"lgp", "--max-frames", "lots", "a", "b", "c"}},
        {"negative number", {"lgp", "--max-frames", "-1", "a", "b", "c"}},
        {"bad option", {"lgp", "--option", "novalue", "a", "b", "c"}},
        {"bad map", {"lgp", "--map", "start", "a", "b", "c"}},
        {"bad gl", {"lgp", "--gl", "three", "a", "b", "c"}},
        {"bad tick", {"lgp", "--tick", "zz", "a", "b", "c"}},
        {"bad watch", {"lgp", "--watch", "x=1:3", "a", "b", "c"}},
        {"expect without a watch", {"lgp", "--expect", "x=1", "a", "b", "c"}},
        {"stop without a watch", {"lgp", "--stop", "x=1", "a", "b", "c"}},
        {"bad tap", {"lgp", "--tap", "180", "--map", "start=8", "a", "b", "c"}},
        {"tap with zero hold", {"lgp", "--tap", "1:8:0", "--map", "start=8", "a", "b", "c"}},
        {"tap without a map", {"lgp", "--tap", "1:8", "a", "b", "c"}},
        {"missing replay file", {"lgp", "--replay", "/nonexistent/replay.input", "--map", "start=8", "a", "b", "c"}},
        {"missing map file", {"lgp", "--map-file", "/nonexistent/map.txt", "a", "b", "c"}},
    };
    unsigned i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        LgpSetup s;
        char *argv[12];
        int argc = 0;
        FILE *err = tmp();
        while (argc < 12 && bad[i].args[argc]) {
            argv[argc] = (char *)bad[i].args[argc];
            ++argc;
        }
        CHECK(parse(&s, err, argc, argv) == LGP_ERR_ARGS, "%s is refused", bad[i].label);
        CHECK(err_has(err, "libretro-guest-probe:"), "%s says why", bad[i].label);
        lgp_setup_free(&s);
        fclose(err);
    }
    {
        LgpSetup s;
        FILE *err = tmp();
        char *argv[] = {"lgp", "--replay", "x", "--tap", "1:8", "--map", "start=8", "a", "b", "c"};
        CHECK(parse(&s, err, 10, argv) == LGP_ERR_ARGS && err_has(err, "cannot be used together"), "replay and tap together");
        lgp_setup_free(&s);
        fclose(err);
    }
}

static void test_taps_overlap_in_order(void)
{
    LgpSetup s;
    PadrSample out[8];
    char *argv[] = {"lgp", "--tap", "50:0x2:5", "--tap", "10:0x1:5", "--tap", "10:0x4:1", "--map", "start=7", "a", "b", "c"};
    CHECK(parse(&s, NULL, 12, argv) == LGP_OK, "taps in any order parse");
    if (s.script) {
        padr_sample(s.script, 9, out, 8);
        CHECK(out[0].buttons == 0, "before the first tap");
        padr_sample(s.script, 11, out, 8);
        CHECK(out[0].buttons == 0x1 || out[0].buttons == 0x4 || out[0].buttons == 0, "taps of one tick follow in file order");
        padr_sample(s.script, 52, out, 8);
        CHECK(out[0].buttons == 0x2, "the later tap");
    }
    lgp_setup_free(&s);
}

static void test_help(void)
{
    LgpSetup s;
    char *help[] = {"lgp", "--help"};
    char *version[] = {"lgp", "--version"};
    CHECK(parse(&s, NULL, 2, help) == LGP_HELP, "--help");
    lgp_setup_free(&s);
    CHECK(parse(&s, NULL, 2, version) == LGP_HELP, "--version");
    lgp_setup_free(&s);
    CHECK(strstr(lgp_usage(), "--replay") && strstr(lgp_usage(), "exit:"), "the usage text");
}

int main(int argc, char **argv)
{
    snprintf(scratch, sizeof(scratch), "%s", argc > 1 ? argv[1] : ".");
    test_everything();
    test_defaults_and_content_dash();
    test_replay_and_map_file();
    test_refusals();
    test_taps_overlap_in_order();
    test_help();
    printf("args_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
