/* A made-up tool built on libretro-guest-probe.
 *
 *   my-guest-probe                        checks its own settings against a made-up block of guest memory (no core needed)
 *   my-guest-probe <core> <content> <dir> runs the core the way the libretro-guest-probe tool would, with the same settings
 */
#include <stdio.h>
#include <string.h>

#include "lgp/lgp.h"

/* the port's own button bits: the replay file and this table agree on them, the package does not know them */
enum { BTN_START = 0x0008, BTN_CROSS = 0x4000 };

static int check_settings(void)
{
    LgpOptions *options = lgp_options_new();
    LgpProbe *probe = lgp_probe_new();
    unsigned char guest[0x200];
    LgpSample sample;
    const char *answer;
    int ok;

    /* core options, answered by pattern */
    lgp_options_parse(options, "*serial*=enabled");
    answer = lgp_options_get(options, "flycast_serial_console", NULL);

    /* a symbol address from the toolchain's nm, a mirrored console address, and what to wait for */
    memset(guest, 0, sizeof(guest));
    guest[0x100] = 9; /* the guest's frame counter */
    guest[0x104] = 3; /* the screen it is on */
    lgp_probe_set_mask(probe, 0x00ffffffu);
    lgp_probe_parse_tick(probe, "0x8c000100:4");
    lgp_probe_parse_watch(probe, "screen=0x8c000104");
    lgp_probe_parse_expect(probe, "screen=3");
    lgp_probe_parse_stop(probe, "screen=3@5");
    sample = lgp_probe_sample(probe, guest, sizeof(guest), 0, stdout);
    ok = answer && strcmp(answer, "enabled") == 0 && sample.ready && sample.tick == 9 && sample.stop &&
         lgp_probe_report(probe, stdout);
    lgp_probe_free(probe);
    lgp_options_free(options);
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    LgpConfig config;
    LgpResult result;
    LgpOptions *options;
    LgpProbe *probe;
    int code;

    if (argc < 4) {
        return check_settings();
    }
    lgp_config_defaults(&config);
    options = lgp_options_new();
    probe = lgp_probe_new();
    config.core_path = argv[1];
    config.content_path = argv[2];
    config.system_dir = argv[3];
    config.options = options;
    config.probe = probe;
    config.map[0].retro_id = (unsigned)lgp_button_id("start");
    config.map[0].mask = BTN_START;
    config.map[1].retro_id = (unsigned)lgp_button_id("b");
    config.map[1].mask = BTN_CROSS;
    config.map_count = 2;
    config.max_frames = 3000;
    code = lgp_run(&config, &result);
    printf("ran %u frames, exit code %d\n", result.frames_run, code);
    lgp_probe_free(probe);
    lgp_options_free(options);
    return code;
}
