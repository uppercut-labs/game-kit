/* A made-up port's settings: a file in the per-user folder, typed reads, a rewrite.
 *
 *   my-port-settings <scratch dir>
 */
#include <stdio.h>
#include <string.h>

#include "pset/pset.h"

int main(int argc, char **argv)
{
    static const char *const MODES[] = { "enhanced", "original" };
    char path[1024];
    PsetOptions opt;
    PortSettings *s;

    /* where the file goes: <per-user folder>/<app>/my-port.cfg; this example keeps it in the folder it is given */
    snprintf(path, sizeof(path), "%s/my-port.cfg", argc > 1 ? argv[1] : ".");
    memset(&opt, 0, sizeof(opt));
    opt.name = "my-port";
    opt.header = "my-port settings. The port rewrites this file: values are kept, comments are not.";
    s = pset_open(path, &opt);
    if (!s) {
        return 1;
    }
    printf("mode %d, size %d%%, fps readout %d\n", pset_get_choice(s, "mode", MODES, 2, 0),
           pset_get_int_step(s, "touch.size", 100, 50, 150, 10), pset_get_bool(s, "fps", 0));
    pset_set(s, "mode", "original");
    pset_set_int(s, "touch.size", 120);
    pset_set_bool(s, "fps", 1);
    if (pset_save(s) != 0) {
        fprintf(stderr, "cannot save %s\n", pset_path(s));
        return 1;
    }
    pset_free(s);
    s = pset_load(path);
    printf("after a restart: mode %d, size %d%%, fps readout %d\n", pset_get_choice(s, "mode", MODES, 2, 0),
           pset_get_int_step(s, "touch.size", 100, 50, 150, 10), pset_get_bool(s, "fps", 0));
    {
        int ok = pset_get_choice(s, "mode", MODES, 2, 0) == 1 && pset_get_int(s, "touch.size", 0) == 120;

        pset_free(s);
        return ok ? 0 : 1;
    }
}
