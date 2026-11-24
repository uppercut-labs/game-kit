/* Scripted input and the rig of plugged-in controllers (pih.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pih/pih.h"

struct PihRig {
    const PihProfiles *profiles;
    PihDevice *slot[PIH_SLOTS];
};

PihRig *pih_rig_new(const PihProfiles *profiles)
{
    PihRig *r = calloc(1, sizeof(*r));

    if (r) {
        r->profiles = profiles;
    }
    return r;
}

void pih_rig_free(PihRig *r)
{
    int i;

    if (!r) {
        return;
    }
    for (i = 0; i < PIH_SLOTS; i++) {
        pih_detach(r->slot[i]);
    }
    free(r);
}

PihDevice *pih_rig_slot(PihRig *r, int slot)
{
    return slot >= 0 && slot < PIH_SLOTS ? r->slot[slot] : NULL;
}

PihDevice *pih_rig_plug(PihRig *r, int slot, const PihProfile *p)
{
    if (slot < 0 || slot >= PIH_SLOTS) {
        return NULL;
    }
    pih_detach(r->slot[slot]);
    r->slot[slot] = pih_attach(p);
    return r->slot[slot];
}

void pih_rig_unplug(PihRig *r, int slot)
{
    if (slot >= 0 && slot < PIH_SLOTS) {
        pih_detach(r->slot[slot]);
        r->slot[slot] = NULL;
    }
}

/* ------------------------------------------------------------------ scripts */

typedef enum { OP_PLUG, OP_UNPLUG, OP_PRESS, OP_RELEASE, OP_HOLD, OP_STICK, OP_AXIS, OP_RELEASE_ALL, OP_WAIT, OP_END } Op;

typedef struct {
    Op op;
    int slot;
    char arg[32];       /* a profile or control */
    int a, b, c;        /* hold: frames; stick: which, x, y; axis: axis, value; wait: frames */
    int line;
} Step;

struct PihScript {
    Step *steps;
    int n;
    int pc;             /* next step */
    int wait;           /* frames left of a wait */
    uint64_t frame;
    struct {
        int slot;
        char control[32];
        int left;
    } holds[32];
    int nholds;
    char name[256];
};

PihScript *pih_script_parse(const char *text, const char *name, char *err, size_t cap)
{
    PihScript *s = calloc(1, sizeof(*s));
    char *copy = strdup(text), *line, *next;
    int ln = 0, space = 0;

    if (!s || !copy) {
        free(s);
        free(copy);
        return NULL;
    }
    snprintf(s->name, sizeof(s->name), "%s", name);
    for (line = copy; line; line = next) {
        char w[32], a1[32], a2[32], a3[32], *c;
        int n, slot;
        Step st;

        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        ln++;
        if ((c = strchr(line, '#')) != NULL) {
            *c = 0;
        }
        n = sscanf(line, "%31s %31s %31s %31s", w, a1, a2, a3);
        if (n <= 0) {
            continue;
        }
        memset(&st, 0, sizeof(st));
        st.line = ln;
        slot = n >= 2 ? atoi(a1) : 0;
        st.slot = slot;
        if (!strcmp(w, "plug") && n == 3) {
            st.op = OP_PLUG;
            snprintf(st.arg, sizeof(st.arg), "%s", a2);
        } else if (!strcmp(w, "unplug") && n == 2) {
            st.op = OP_UNPLUG;
        } else if ((!strcmp(w, "press") || !strcmp(w, "release")) && n == 3) {
            st.op = w[0] == 'p' ? OP_PRESS : OP_RELEASE;
            snprintf(st.arg, sizeof(st.arg), "%s", a2);
        } else if (!strcmp(w, "hold") && n == 4) {
            st.op = OP_HOLD;
            snprintf(st.arg, sizeof(st.arg), "%s", a2);
            st.a = atoi(a3);
        } else if (!strcmp(w, "release-all") && n == 2) {
            st.op = OP_RELEASE_ALL;
        } else if (!strcmp(w, "wait") && n == 2) {
            st.op = OP_WAIT;
            st.a = atoi(a1);
        } else if (!strcmp(w, "end") && n == 1) {
            st.op = OP_END;
        } else if ((!strcmp(w, "stick") || !strcmp(w, "axis")) && n >= 3) {
            char rest[32] = "";

            if (w[0] == 's') {
                if (sscanf(line, "%*s %*s %31s %i %i", rest, &st.b, &st.c) != 3 || (strcmp(rest, "left") && strcmp(rest, "right"))) {
                    goto bad;
                }
                st.op = OP_STICK;
                st.a = !strcmp(rest, "right");
            } else {
                if (sscanf(line, "%*s %*s %i %i", &st.a, &st.b) != 2) {
                    goto bad;
                }
                st.op = OP_AXIS;
            }
        } else {
            goto bad;
        }
        if (s->n == space) {
            Step *t = realloc(s->steps, sizeof(Step) * (size_t)(space ? 2 * space : 64));

            if (!t) {
                goto bad_mem;
            }
            s->steps = t;
            space = space ? 2 * space : 64;
        }
        s->steps[s->n++] = st;
        continue;
    bad:
        if (err && cap) {
            snprintf(err, cap, "%s:%d: not a script line", name, ln);
        }
        free(copy);
        pih_script_free(s);
        return NULL;
    }
    free(copy);
    return s;
bad_mem:
    free(copy);
    pih_script_free(s);
    return NULL;
}

PihScript *pih_script_load(const char *path, char *err, size_t cap)
{
    size_t len = 0;
    char *raw = SDL_LoadFile(path, &len), *text;
    PihScript *s;

    if (!raw) {
        if (err && cap) {
            snprintf(err, cap, "cannot read %s: %s", path, SDL_GetError());
        }
        return NULL;
    }
    text = malloc(len + 1);
    if (!text) {
        SDL_free(raw);
        return NULL;
    }
    memcpy(text, raw, len);
    text[len] = 0;
    SDL_free(raw);
    s = pih_script_parse(text, path, err, cap);
    free(text);
    return s;
}

void pih_script_free(PihScript *s)
{
    if (s) {
        free(s->steps);
        free(s);
    }
}

uint64_t pih_script_frames(const PihScript *s)
{
    return s->frame;
}

int pih_script_frame(PihScript *s, PihRig *r)
{
    int i, more = 1;

    /* the frame's holds count down first: one pressed for n frames is released at the start of the nth after */
    for (i = 0; i < s->nholds;) {
        if (--s->holds[i].left <= 0) {
            PihDevice *d = pih_rig_slot(r, s->holds[i].slot);

            if (d) {
                pih_press(d, s->holds[i].control, 0);
            }
            s->holds[i] = s->holds[--s->nholds];
        } else {
            i++;
        }
    }
    if (s->wait > 0) {
        s->wait--;
    }
    while (s->wait == 0 && s->pc < s->n && more) {
        const Step *st = &s->steps[s->pc++];
        PihDevice *d = pih_rig_slot(r, st->slot);

        switch (st->op) {
        case OP_PLUG: {
            const PihProfile *p = pih_profiles_find(r->profiles, st->arg);

            if (p) {
                pih_rig_plug(r, st->slot, p);
            } else {
                fprintf(stderr, "%s:%d: no profile %s\n", s->name, st->line, st->arg);
            }
            break;
        }
        case OP_UNPLUG:
            pih_rig_unplug(r, st->slot);
            break;
        case OP_PRESS:
        case OP_RELEASE:
            if (d && !pih_press(d, st->arg, st->op == OP_PRESS)) {
                fprintf(stderr, "%s:%d: %s has no control %s\n", s->name, st->line, pih_profile(d)->name, st->arg);
            }
            break;
        case OP_HOLD:
            if (d && pih_press(d, st->arg, 1) && s->nholds < 32) {
                s->holds[s->nholds].slot = st->slot;
                snprintf(s->holds[s->nholds].control, sizeof(s->holds[0].control), "%s", st->arg);
                s->holds[s->nholds++].left = st->a > 0 ? st->a : 1;
            }
            break;
        case OP_RELEASE_ALL:
            if (d) {
                pih_release_all(d);
            }
            break;
        case OP_STICK:
            if (d) {
                pih_stick(d, st->a, (uint8_t)st->b, (uint8_t)st->c);
            }
            break;
        case OP_AXIS:
            if (d) {
                pih_raw_axis(d, st->a, st->b);
            }
            break;
        case OP_WAIT:
            s->wait = st->a;
            break;
        case OP_END:
            more = 0;
            break;
        }
    }
    s->frame++;
    if (!more || (s->pc >= s->n && s->wait == 0)) {
        return 0;
    }
    return 1;
}
