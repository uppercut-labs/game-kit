/* The check runner: a port describes its input in a PihAdapter, the runner attaches every profile and checks it,
 * and prints one table, a row per check and a column per profile, pass or FAIL. Exit code 1 if anything failed.
 *
 * The port's test is a main() that fills in an adapter and calls pih_main. The generic checks need only read():
 *
 *   - SDL's identity for the profile (its gamepad type), and the family the port detects, if it says
 *   - at rest the port reads nothing, on every player
 *   - every control alone gives exactly the bits the map says, and nothing when released
 *   - every bit of the map through pih_hold, one at a time, and all of them at once
 *   - unplugged, the port reads nothing
 *   - two controllers are two players, in the order plugged in; a controller unplugged with a button held leaves
 *     nothing stuck; a replacement takes the free player; replugging works; a flash of a controller (plugged and
 *     unplugged before the port looks) leaves nothing; a third controller shares player 0 (if the adapter says so)
 *   - a scripted sequence (press, hold, wait, release) reads as it should
 *
 * and the adapter's own checks (extra) add its rules, through pih_check.
 */
#ifndef PIHRUN_H
#define PIHRUN_H

#include <stdint.h>

#include "pih.h"

typedef struct PihRun PihRun;

typedef struct {
    const char *name;                /* the port, for the heading */
    void *user;
    /* A fresh input layer under test, as the port starts it, with nothing plugged in and only virtual controllers
       counting. NULL on failure. close frees it. */
    void *(*open)(void *user);
    void (*close)(void *user, void *input);
    /* The port looks: handles SDL's events (SDL_UpdateJoysticks, SDL_PollEvent) and decides one frame of input. */
    void (*pump)(void *user, void *input);
    /* The port's state for a player as bits (its own meaning, the map's), after pump. */
    uint32_t (*read)(void *user, void *input, int player);
    /* The family the port thinks the last used controller is (a profile's family string), or NULL to skip. */
    const char *(*family)(void *user, void *input);
    /* How many players the port has (usually 2), and whether a third controller shares player 0. */
    int players;
    int third_shares_first;
    /* Bits the "all at once" check leaves out (a port that cancels opposite directions says which). */
    uint32_t chord_mask;
    /* Its own checks for one profile; called with the layer open and nothing plugged in. May be NULL. */
    void (*extra)(PihRun *run, const PihProfile *p, void *user);
    /* Its own checks that need several profiles or layers of their own. May be NULL. */
    void (*extra_all)(PihRun *run, const PihProfiles *all, void *user);
} PihAdapter;

/* Records one result: the check is a table row, the profile a column (NULL: a check that isn't about one). */
void pih_check(PihRun *run, const PihProfile *p, const char *check, int ok, const char *fmt, ...);
/* The pieces an adapter's checks use. */
void *pih_run_input(PihRun *run);              /* the open layer the extra checks work on */
void pih_run_pump(PihRun *run);
uint32_t pih_run_read(PihRun *run, int player);
PihMap *pih_run_map(PihRun *run);
const PihAdapter *pih_run_adapter(PihRun *run);
/* Another layer for a check of its own, and its end. */
void *pih_run_reopen(PihRun *run);
void pih_run_close(PihRun *run, void *input);

/* argv: --profiles <dir> --map <file> [--only <profile>[,<profile>]] [-v]. Initialises SDL (HIDAPI off, so only the
   virtual controllers exist), runs, prints the table, returns the exit code. */
int pih_main(const PihAdapter *a, int argc, char **argv);

#endif
