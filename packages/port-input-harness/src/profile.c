/* Controller profiles from text files (pih.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pih/pih.h"

#define MAX_PROFILES 32

struct PihProfiles {
    PihProfile *p;
    int n;
};

/* ------------------------------------------------------------------ text */

static int tokens(char *line, char *tok[], int max)
{
    int n = 0;
    char *c = strchr(line, '#');

    if (c) {
        *c = 0;
    }
    for (c = line; *c && n < max;) {
        while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
            *c++ = 0;
        }
        if (!*c) {
            break;
        }
        tok[n++] = c;
        while (*c && *c != ' ' && *c != '\t' && *c != '\r' && *c != '\n') {
            c++;
        }
    }
    return n;
}

static int fail(char *err, size_t cap, const char *file, int line, const char *what)
{
    if (err && cap) {
        snprintf(err, cap, "%s:%d: %s", file, line, what);
    }
    return -1;
}

static int hat_bit(const char *s)
{
    return !strcmp(s, "up") ? SDL_HAT_UP : !strcmp(s, "right") ? SDL_HAT_RIGHT : !strcmp(s, "down") ? SDL_HAT_DOWN
         : !strcmp(s, "left") ? SDL_HAT_LEFT : 0;
}

static const struct {
    const char *name;
    SDL_JoystickType type;
} JOYTYPES[] = {
    { "gamepad", SDL_JOYSTICK_TYPE_GAMEPAD },         { "unknown", SDL_JOYSTICK_TYPE_UNKNOWN },
    { "arcade_stick", SDL_JOYSTICK_TYPE_ARCADE_STICK }, { "wheel", SDL_JOYSTICK_TYPE_WHEEL },
    { "flight_stick", SDL_JOYSTICK_TYPE_FLIGHT_STICK }, { "arcade_pad", SDL_JOYSTICK_TYPE_ARCADE_PAD },
};

static int parse(PihProfile *p, const char *path, const char *dir, int depth, char *err, size_t cap);

static int parse_text(PihProfile *p, char *text, const char *file, const char *dir, int depth, char *err, size_t cap)
{
    char *line, *next;
    int ln = 0;

    for (line = text; line; line = next) {
        char *tok[8], raw[256];
        int n;

        next = strchr(line, '\n');
        if (next) {
            *next++ = 0;
        }
        ln++;
        snprintf(raw, sizeof(raw), "%s", line);
        n = tokens(line, tok, 8);
        if (!n) {
            continue;
        }
        if (!strcmp(tok[0], "device_name")) {
            const char *v = raw;
            char *c;

            while (*v == ' ' || *v == '\t') {
                v++;
            }
            v += strlen("device_name");
            while (*v == ' ' || *v == '\t') {
                v++;
            }
            snprintf(p->device_name, sizeof(p->device_name), "%s", v);
            for (c = p->device_name + strlen(p->device_name); c > p->device_name && (c[-1] == '\r' || c[-1] == ' '); *--c = 0) {
            }
        } else if (!strcmp(tok[0], "name") && n == 2) {
            snprintf(p->name, sizeof(p->name), "%s", tok[1]);
        } else if (!strcmp(tok[0], "vendor") && n == 2) {
            p->vendor = (Uint16)strtoul(tok[1], NULL, 0);
        } else if (!strcmp(tok[0], "product") && n == 2) {
            p->product = (Uint16)strtoul(tok[1], NULL, 0);
        } else if (!strcmp(tok[0], "layout") && n == 2) {
            snprintf(p->layout, sizeof(p->layout), "%s", tok[1]);
            p->gamepad = !strcmp(tok[1], "gamepad");
        } else if (!strcmp(tok[0], "family") && n == 2) {
            snprintf(p->family, sizeof(p->family), "%s", tok[1]);
        } else if (!strcmp(tok[0], "gamepad_type") && n >= 2) {
            /* one or more of SDL's type names; each SDL release names the types it knows, so a profile lists what an
               older SDL reports after the newer one (steam standard): any SDL-known name in the list passes */
            int k;

            p->gamepad_type = SDL_GAMEPAD_TYPE_UNKNOWN;
            p->ngamepad_types = 0;
            for (k = 1; k < n; k++) {
                SDL_GamepadType t = SDL_GetGamepadTypeFromString(tok[k]);

                if (t == SDL_GAMEPAD_TYPE_UNKNOWN || p->ngamepad_types >= 4) {
                    continue;
                }
                if (!p->ngamepad_types) {
                    p->gamepad_type = t;
                }
                p->gamepad_types[p->ngamepad_types++] = t;
            }
        } else if (!strcmp(tok[0], "joytype") && n == 2) {
            size_t i;

            for (i = 0; i < sizeof(JOYTYPES) / sizeof(JOYTYPES[0]); i++) {
                if (!strcmp(tok[1], JOYTYPES[i].name)) {
                    p->joytype = JOYTYPES[i].type;
                    break;
                }
            }
            if (i == sizeof(JOYTYPES) / sizeof(JOYTYPES[0])) {
                return fail(err, cap, file, ln, "unknown joytype");
            }
        } else if (!strcmp(tok[0], "axes") && n == 2) {
            p->naxes = atoi(tok[1]);
        } else if (!strcmp(tok[0], "buttons") && n == 2) {
            p->nbuttons = atoi(tok[1]);
        } else if (!strcmp(tok[0], "hats") && n == 2) {
            p->nhats = atoi(tok[1]);
        } else if (!strcmp(tok[0], "include") && n == 2) {
            char inc[1024];

            if (depth > 4) {
                return fail(err, cap, file, ln, "includes nest too deep");
            }
            snprintf(inc, sizeof(inc), "%s/%s", dir, tok[1]);
            if (parse(p, inc, dir, depth + 1, err, cap) != 0) {
                return -1;
            }
        } else if (!strcmp(tok[0], "control") && n >= 4) {
            PihControl *c;

            if (p->ncontrols >= PIH_MAX_CONTROLS) {
                return fail(err, cap, file, ln, "too many controls");
            }
            c = &p->controls[p->ncontrols];
            memset(c, 0, sizeof(*c));
            snprintf(c->name, sizeof(c->name), "%s", tok[1]);
            if (!strcmp(tok[2], "gbutton")) {
                SDL_GamepadButton b = SDL_GetGamepadButtonFromString(tok[3]);

                if (b == SDL_GAMEPAD_BUTTON_INVALID) {
                    return fail(err, cap, file, ln, "not an SDL gamepad button");
                }
                c->type = PIH_BUTTON;
                c->index = (int)b;
            } else if (!strcmp(tok[2], "gaxis") && n == 6) {
                SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(tok[3]);

                if (a == SDL_GAMEPAD_AXIS_INVALID) {
                    return fail(err, cap, file, ln, "not an SDL gamepad axis");
                }
                c->type = PIH_AXIS;
                c->index = (int)a;
                c->value = atoi(tok[4]);
                c->rest = atoi(tok[5]);
            } else if (!strcmp(tok[2], "jbutton")) {
                c->type = PIH_BUTTON;
                c->index = atoi(tok[3]);
            } else if (!strcmp(tok[2], "jaxis") && n == 6) {
                c->type = PIH_AXIS;
                c->index = atoi(tok[3]);
                c->value = atoi(tok[4]);
                c->rest = atoi(tok[5]);
            } else if (!strcmp(tok[2], "jhat") && n == 5 && hat_bit(tok[4])) {
                c->type = PIH_HAT;
                c->index = atoi(tok[3]);
                c->value = hat_bit(tok[4]);
            } else {
                return fail(err, cap, file, ln, "a control is: control <name> gbutton|gaxis|jbutton|jaxis|jhat ...");
            }
            p->ncontrols++;
        } else {
            return fail(err, cap, file, ln, "not a profile line");
        }
    }
    return 0;
}

static int parse(PihProfile *p, const char *path, const char *dir, int depth, char *err, size_t cap)
{
    size_t len = 0;
    char *text = SDL_LoadFile(path, &len), *copy;
    int rc;

    if (!text) {
        if (err && cap) {
            snprintf(err, cap, "cannot read %s: %s", path, SDL_GetError());
        }
        return -1;
    }
    copy = malloc(len + 1);
    if (!copy) {
        SDL_free(text);
        return -1;
    }
    memcpy(copy, text, len);
    copy[len] = 0;
    SDL_free(text);
    rc = parse_text(p, copy, path, dir, depth, err, cap);
    free(copy);
    return rc;
}

/* ------------------------------------------------------------------ the set */

typedef struct {
    char names[MAX_PROFILES][256];
    int n;
} Listing;

static SDL_EnumerationResult SDLCALL on_entry(void *ud, const char *dirname, const char *fname)
{
    Listing *l = ud;
    size_t len = strlen(fname);

    (void)dirname;
    if (len > 8 && !strcmp(fname + len - 8, ".profile") && l->n < MAX_PROFILES) {
        snprintf(l->names[l->n++], sizeof(l->names[0]), "%s", fname);
    }
    return SDL_ENUM_CONTINUE;
}

static int by_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

PihProfiles *pih_profiles_load(const char *dir, char *err, size_t cap)
{
    Listing *l = calloc(1, sizeof(*l));
    PihProfiles *ps = calloc(1, sizeof(*ps));
    int i;

    if (!l || !ps) {
        free(l);
        free(ps);
        return NULL;
    }
    if (!SDL_EnumerateDirectory(dir, on_entry, l) || !l->n) {
        if (err && cap) {
            snprintf(err, cap, "no *.profile files in %s", dir);
        }
        goto bad;
    }
    qsort(l->names, (size_t)l->n, sizeof(l->names[0]), by_name);
    ps->p = calloc((size_t)l->n, sizeof(PihProfile));
    if (!ps->p) {
        goto bad;
    }
    for (i = 0; i < l->n; i++) {
        char path[1100];
        PihProfile *p = &ps->p[ps->n];

        snprintf(path, sizeof(path), "%s/%s", dir, l->names[i]);
        p->gamepad_type = SDL_GAMEPAD_TYPE_UNKNOWN;
        if (parse(p, path, dir, 0, err, cap) != 0) {
            goto bad;
        }
        if (!p->name[0] || !p->vendor || !p->layout[0]) {
            if (err && cap) {
                snprintf(err, cap, "%s: needs name, vendor and layout", path);
            }
            goto bad;
        }
        ps->n++;
    }
    free(l);
    return ps;
bad:
    free(l);
    pih_profiles_free(ps);
    return NULL;
}

void pih_profiles_free(PihProfiles *p)
{
    if (p) {
        free(p->p);
        free(p);
    }
}

int pih_profiles_count(const PihProfiles *p)
{
    return p->n;
}

const PihProfile *pih_profiles_get(const PihProfiles *p, int i)
{
    return i >= 0 && i < p->n ? &p->p[i] : NULL;
}

const PihProfile *pih_profiles_find(const PihProfiles *p, const char *name)
{
    int i;

    for (i = 0; i < p->n; i++) {
        if (!strcmp(p->p[i].name, name)) {
            return &p->p[i];
        }
    }
    return NULL;
}

const PihControl *pih_find_control(const PihProfile *p, const char *name)
{
    int i;

    for (i = 0; i < p->ncontrols; i++) {
        if (!strcmp(p->controls[i].name, name)) {
            return &p->controls[i];
        }
    }
    return NULL;
}
