/* The check runner (pihrun.h). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pih/pihrun.h"

typedef struct {
    int row;        /* index into names */
    int col;        /* profile index, or -1 */
    int ok;
    char detail[200];
} Result;

struct PihRun {
    const PihAdapter *a;
    const PihProfiles *profiles;
    PihMap *map;
    void *input;
    int verbose;
    char **rows;
    int nrows;
    Result *res;
    int nres, capres;
    int failed;
};

static int row_of(PihRun *r, const char *name)
{
    int i;

    for (i = 0; i < r->nrows; i++) {
        if (!strcmp(r->rows[i], name)) {
            return i;
        }
    }
    r->rows = realloc(r->rows, sizeof(char *) * (size_t)(r->nrows + 1));
    r->rows[r->nrows] = strdup(name);
    return r->nrows++;
}

static int col_of(const PihRun *r, const PihProfile *p)
{
    int i;

    for (i = 0; p && i < pih_profiles_count(r->profiles); i++) {
        if (pih_profiles_get(r->profiles, i) == p) {
            return i;
        }
    }
    return -1;
}

void pih_check(PihRun *r, const PihProfile *p, const char *check, int ok, const char *fmt, ...)
{
    Result *x;
    va_list ap;

    if (r->nres == r->capres) {
        r->capres = r->capres ? 2 * r->capres : 256;
        r->res = realloc(r->res, sizeof(Result) * (size_t)r->capres);
    }
    x = &r->res[r->nres++];
    x->row = row_of(r, check);
    x->col = col_of(r, p);
    x->ok = ok;
    x->detail[0] = 0;
    if (!ok && fmt) {
        va_start(ap, fmt);
        vsnprintf(x->detail, sizeof(x->detail), fmt, ap);
        va_end(ap);
    }
    if (!ok) {
        r->failed++;
    }
}

void *pih_run_input(PihRun *r)
{
    return r->input;
}

void pih_run_pump(PihRun *r)
{
    r->a->pump(r->a->user, r->input);
}

uint32_t pih_run_read(PihRun *r, int player)
{
    return r->a->read(r->a->user, r->input, player);
}

PihMap *pih_run_map(PihRun *r)
{
    return r->map;
}

const PihAdapter *pih_run_adapter(PihRun *r)
{
    return r->a;
}

void *pih_run_reopen(PihRun *r)
{
    return r->a->open(r->a->user);
}

void pih_run_close(PihRun *r, void *input)
{
    r->a->close(r->a->user, input);
}

/* ------------------------------------------------------------------ the generic checks */

/* By value: the map's array is reused by its next call. */
static int first_primary(PihMap *m, const PihProfile *p, PihMapEntry *out)
{
    int n, i;
    const PihMapEntry *e = pih_map_entries(m, p, &n);

    for (i = 0; i < n; i++) {
        if (e[i].primary && e[i].bits) {
            *out = e[i];
            return 1;
        }
    }
    return 0;
}

/* all players read nothing */
static int silent(PihRun *r, char *why, size_t cap)
{
    int pl;

    pih_run_pump(r);
    for (pl = 0; pl < r->a->players; pl++) {
        uint32_t v = pih_run_read(r, pl);

        if (v) {
            snprintf(why, cap, "player %d reads %#x", pl, (unsigned)v);
            return 0;
        }
    }
    return 1;
}

static const PihProfile *partner(PihRun *r, const PihProfile *p)
{
    int n = pih_profiles_count(r->profiles), i;

    for (i = 0; i < n; i++) {
        if (pih_profiles_get(r->profiles, i) == p) {
            return pih_profiles_get(r->profiles, (i + 1) % n);
        }
    }
    return p;
}

static void generic(PihRun *r, const PihProfile *p)
{
    char why[160];
    PihDevice *d;
    int n, i, ok;
    const PihMapEntry *e;
    uint32_t all = 0;

    r->input = r->a->open(r->a->user);
    if (!r->input) {
        pih_check(r, p, "the port's input layer opens", 0, "open failed");
        return;
    }
    d = pih_attach(p);
    if (!d) {
        pih_check(r, p, "attaches", 0, "%s", SDL_GetError());
        r->a->close(r->a->user, r->input);
        return;
    }
    pih_run_pump(r);

    /* SDL's identity */
    ok = SDL_IsGamepad(pih_id(d)) == (p->gamepad != 0);
    if (ok && p->gamepad && p->ngamepad_types) {
        SDL_GamepadType got = SDL_GetGamepadTypeForID(pih_id(d));
        int k;

        ok = 0;
        for (k = 0; k < p->ngamepad_types; k++) {
            ok |= got == p->gamepad_types[k];
        }
    }
    pih_check(r, p, "SDL's identity: gamepad mapping and type", ok, "SDL says gamepad %d, type %d", SDL_IsGamepad(pih_id(d)),
              (int)SDL_GetGamepadTypeForID(pih_id(d)));
    if (r->a->family) {
        const char *f = r->a->family(r->a->user, r->input);

        pih_check(r, p, "the port detects the family", f && !strcmp(f, p->family), "%s, not %s", f ? f : "(none)", p->family);
    }
    ok = silent(r, why, sizeof(why));
    pih_check(r, p, "nothing pressed: nothing read", ok, "%s", why);

    /* every control alone */
    e = pih_map_entries(r->map, p, &n);
    why[0] = 0;
    ok = 1;
    for (i = 0; i < n && ok; i++) {
        uint32_t v, w;

        pih_set_control(d, e[i].control, 1);
        pih_run_pump(r);
        v = pih_run_read(r, 0);
        w = r->a->players > 1 ? pih_run_read(r, 1) : 0;
        if (v != e[i].bits || w) {
            snprintf(why, sizeof(why), "%s gave %#x (player 2: %#x), not %#x", e[i].control->name, (unsigned)v, (unsigned)w,
                     (unsigned)e[i].bits);
            ok = 0;
        }
        pih_set_control(d, e[i].control, 0);
        pih_run_pump(r);
        if (ok && pih_run_read(r, 0)) {
            snprintf(why, sizeof(why), "%s released, still reads %#x", e[i].control->name, (unsigned)pih_run_read(r, 0));
            ok = 0;
        }
    }
    pih_check(r, p, "every control alone gives what the map says", ok, "%s", why);

    /* every primary bit through pih_hold, and all of them at once */
    ok = 1;
    why[0] = 0;
    for (i = 0; i < n; i++) {
        all |= e[i].primary ? e[i].bits : 0;
    }
    for (i = 0; i < n && ok; i++) {
        uint32_t miss, v;

        if (!e[i].primary || !e[i].bits) {
            continue;
        }
        miss = pih_hold(d, r->map, e[i].bits);
        pih_run_pump(r);
        v = pih_run_read(r, 0);
        if (miss || v != e[i].bits) {
            snprintf(why, sizeof(why), "holding %#x: read %#x, unreachable %#x", (unsigned)e[i].bits, (unsigned)v, (unsigned)miss);
            ok = 0;
        }
    }
    pih_check(r, p, "holding each mapped bit gives it", ok, "%s", why);
    {
        uint32_t want = all & ~r->a->chord_mask, miss, v;

        miss = pih_hold(d, r->map, want);
        pih_run_pump(r);
        v = pih_run_read(r, 0);
        pih_check(r, p, "every mapped bit at once", !miss && v == want, "wanted %#x, read %#x, unreachable %#x", (unsigned)want,
                  (unsigned)v, (unsigned)miss);
        pih_hold(d, r->map, 0);
        pih_run_pump(r);
        pih_check(r, p, "released: nothing read", pih_run_read(r, 0) == 0, "reads %#x", (unsigned)pih_run_read(r, 0));
    }

    /* unplugged */
    pih_detach(d);
    ok = silent(r, why, sizeof(why));
    pih_check(r, p, "unplugged: nothing read", ok, "%s", why);
    r->a->close(r->a->user, r->input);
    r->input = NULL;
}

static void several(PihRun *r, const PihProfile *p)
{
    const PihProfile *q = partner(r, p);
    PihMapEntry vp, vq;
    const PihMapEntry *bp = &vp, *bq = &vq;
    PihDevice *a, *b, *c;
    uint32_t x, y;
    char why[160];

    if (!first_primary(r->map, p, &vp) || !first_primary(r->map, q, &vq) || r->a->players < 2) {
        return;
    }
    r->input = r->a->open(r->a->user);
    if (!r->input) {
        return;
    }
    /* two players, in the order plugged in */
    a = pih_attach(p);
    pih_run_pump(r);
    b = pih_attach(q);
    pih_run_pump(r);
    pih_set_control(a, bp->control, 1);
    pih_set_control(b, bq->control, 1);
    pih_run_pump(r);
    x = pih_run_read(r, 0);
    y = pih_run_read(r, 1);
    pih_check(r, p, "two controllers are players 1 and 2 in plug order", x == bp->bits && y == bq->bits,
              "player 1 read %#x (wanted %#x), player 2 %#x (wanted %#x)", (unsigned)x, (unsigned)bp->bits, (unsigned)y,
              (unsigned)bq->bits);
    /* player 1's controller leaves with a button held */
    pih_detach(a);
    pih_run_pump(r);
    x = pih_run_read(r, 0);
    y = pih_run_read(r, 1);
    pih_check(r, p, "unplugged with a button held: nothing stuck, the other plays on", x == 0 && y == bq->bits,
              "player 1 read %#x, player 2 %#x", (unsigned)x, (unsigned)y);
    /* a replacement takes the free place */
    a = pih_attach(p);
    pih_run_pump(r);
    pih_set_control(a, bp->control, 1);
    pih_run_pump(r);
    x = pih_run_read(r, 0);
    y = pih_run_read(r, 1);
    pih_check(r, p, "a replacement takes the free player", x == bp->bits && y == bq->bits, "read %#x and %#x", (unsigned)x,
              (unsigned)y);
    /* a third */
    if (r->a->third_shares_first) {
        pih_set_control(a, bp->control, 0);
        pih_set_control(b, bq->control, 0);
        c = pih_attach(p);
        pih_run_pump(r);
        pih_set_control(c, bp->control, 1);
        pih_run_pump(r);
        x = pih_run_read(r, 0);
        y = pih_run_read(r, 1);
        pih_check(r, p, "a third controller shares player 1", x == bp->bits && y == 0, "read %#x and %#x", (unsigned)x,
                  (unsigned)y);
        pih_detach(c);
    }
    pih_detach(a);
    pih_detach(b);
    pih_run_pump(r);
    /* a flash of a controller: plugged and unplugged before the port looks */
    a = pih_attach(p);
    pih_detach(a);
    pih_run_pump(r);
    pih_check(r, p, "a controller that comes and goes before the port looks leaves nothing", silent(r, why, sizeof(why)), "%s",
              why);
    a = pih_attach(p);
    pih_run_pump(r);
    pih_set_control(a, bp->control, 1);
    pih_run_pump(r);
    pih_check(r, p, "and the port still plays after it", pih_run_read(r, 0) == bp->bits, "reads %#x",
              (unsigned)pih_run_read(r, 0));
    pih_detach(a);
    pih_run_pump(r);
    r->a->close(r->a->user, r->input);
    r->input = NULL;
}

static void scripted(PihRun *r, const PihProfile *p)
{
    PihMapEntry vp;
    const PihMapEntry *bp = &vp;
    char text[256], why[160] = "";
    PihScript *s;
    PihRig *rig;
    int f, ok = 1;

    if (!first_primary(r->map, p, &vp)) {
        return;
    }
    snprintf(text, sizeof(text), "plug 0 %s\nwait 1\nhold 0 %s 3\nwait 6\nend\n", p->name, bp->control->name);
    s = pih_script_parse(text, "scripted", why, sizeof(why));
    r->input = r->a->open(r->a->user);
    rig = pih_rig_new(r->profiles);
    if (!s || !r->input || !rig) {
        pih_check(r, p, "a scripted press reads for exactly its frames", 0, "%s", why);
        goto out;
    }
    for (f = 0; f < 8; f++) {
        uint32_t v, want = (f >= 1 && f <= 3) ? bp->bits : 0;

        pih_script_frame(s, rig);
        pih_run_pump(r);
        v = pih_run_read(r, 0);
        if (v != want && ok) {
            snprintf(why, sizeof(why), "frame %d read %#x, wanted %#x", f, (unsigned)v, (unsigned)want);
            ok = 0;
        }
    }
    pih_check(r, p, "a scripted press reads for exactly its frames", ok, "%s", why);
out:
    pih_rig_free(rig);
    pih_script_free(s);
    if (r->input) {
        pih_run_pump(r);
        r->a->close(r->a->user, r->input);
        r->input = NULL;
    }
}

/* ------------------------------------------------------------------ the table */

static void print_table(PihRun *r)
{
    int ncol = pih_profiles_count(r->profiles), i, c, any_general = 0;
    int w = 12, lw = 8;
    int *used = calloc((size_t)(ncol + 1), sizeof(int));
    const char **cell = calloc((size_t)r->nrows * (size_t)(ncol + 1), sizeof(char *));

    for (i = 0; i < r->nrows; i++) {
        lw = (int)strlen(r->rows[i]) > lw ? (int)strlen(r->rows[i]) : lw;
    }
    for (i = 0; i < r->nres; i++) {
        const Result *x = &r->res[i];
        int col = x->col < 0 ? ncol : x->col;
        const char **cp = &cell[x->row * (ncol + 1) + col];

        used[col] = 1;
        if (x->col < 0) {
            any_general = 1;
        }
        if (*cp == NULL || !x->ok) {
            *cp = x->ok ? "pass" : "FAIL";
        }
    }
    printf("\n%-*s", lw, "check");
    for (c = 0; c < ncol; c++) {
        printf(" %-*s", w, pih_profiles_get(r->profiles, c)->name);
    }
    if (any_general) {
        printf(" %-*s", w, "(general)");
    }
    printf("\n");
    for (i = 0; i < r->nrows; i++) {
        printf("%-*s", lw, r->rows[i]);
        for (c = 0; c <= ncol; c++) {
            if (c == ncol && !any_general) {
                continue;
            }
            printf(" %-*s", w, cell[i * (ncol + 1) + c] ? cell[i * (ncol + 1) + c] : ".");
        }
        printf("\n");
    }
    for (i = 0; i < r->nres; i++) {
        const Result *x = &r->res[i];

        if (!x->ok) {
            printf("FAIL %s [%s]: %s\n", r->rows[x->row], x->col >= 0 ? pih_profiles_get(r->profiles, x->col)->name : "general",
                   x->detail);
        }
    }
    free(used);
    free(cell);
}

int pih_main(const PihAdapter *a, int argc, char **argv)
{
    const char *profiles_dir = NULL, *map_path = NULL, *only = NULL;
    char err[300];
    PihRun run;
    PihProfiles *ps;
    int i, passed;

    memset(&run, 0, sizeof(run));
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--profiles") && i + 1 < argc) {
            profiles_dir = argv[++i];
        } else if (!strcmp(argv[i], "--map") && i + 1 < argc) {
            map_path = argv[++i];
        } else if (!strcmp(argv[i], "--only") && i + 1 < argc) {
            only = argv[++i];
        } else if (!strcmp(argv[i], "-v")) {
            run.verbose = 1;
        } else {
            fprintf(stderr, "usage: %s --profiles <dir> --map <file> [--only <profile>[,<profile>]] [-v]\n", a->name);
            return 64;
        }
    }
    if (!profiles_dir || !map_path) {
        fprintf(stderr, "usage: %s --profiles <dir> --map <file> [--only <profile>[,<profile>]] [-v]\n", a->name);
        return 64;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");   /* only the virtual controllers */
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "%s: SDL: %s\n", a->name, SDL_GetError());
        return 1;
    }
    ps = pih_profiles_load(profiles_dir, err, sizeof(err));
    run.map = pih_map_load(map_path, err + 0, sizeof(err));
    if (!ps || !run.map) {
        fprintf(stderr, "%s: %s\n", a->name, err);
        return 1;
    }
    run.a = a;
    run.profiles = ps;
    for (i = 0; i < pih_profiles_count(ps); i++) {
        const PihProfile *p = pih_profiles_get(ps, i);

        if (only) {
            char list[256], *t, *save = NULL;
            int hit = 0;

            snprintf(list, sizeof(list), "%s", only);
            for (t = SDL_strtok_r(list, ",", &save); t; t = SDL_strtok_r(NULL, ",", &save)) {
                hit |= !strcmp(t, p->name);
            }
            if (!hit) {
                continue;
            }
        }
        if (run.verbose) {
            printf("%s: %s\n", a->name, p->name);
        }
        generic(&run, p);
        several(&run, p);
        scripted(&run, p);
        if (a->extra) {
            run.input = a->open(a->user);
            if (run.input) {
                a->extra(&run, p, a->user);
                a->close(a->user, run.input);
                run.input = NULL;
            }
        }
    }
    if (a->extra_all) {
        a->extra_all(&run, ps, a->user);
    }
    print_table(&run);
    passed = run.nres - run.failed;
    printf("\n%s: %d of %d checks passed\n", a->name, passed, run.nres);
    pih_profiles_free(ps);
    pih_map_free(run.map);
    SDL_Quit();
    return run.failed ? 1 : 0;
}
