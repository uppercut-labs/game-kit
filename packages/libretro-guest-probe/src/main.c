/* libretro-guest-probe: the command line. */
#include <stdio.h>

#include "lgp/lgp.h"

int main(int argc, char **argv)
{
    LgpSetup setup;
    LgpResult result;
    int code = lgp_args_parse(argc, argv, &setup, stderr);
    if (code == LGP_HELP) {
        lgp_setup_free(&setup);
        return 0;
    }
    if (code == LGP_ERR_ARGS) {
        fputs("try --help\n", stderr);
        lgp_setup_free(&setup);
        return LGP_ERR_ARGS;
    }
    code = lgp_run(&setup.config, &result);
    lgp_setup_free(&setup);
    return code;
}
