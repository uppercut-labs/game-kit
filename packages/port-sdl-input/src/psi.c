/* port-sdl-input (psi.h). */
#include "psi/psi.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bind.h"

#define MAX_BIND 8
#define MAX_DEV 16
#define STICK_ON 16384      /* half way */
#define TRIGGER_ON 8192     /* a quarter */
#define CAPTURE_AXIS 20000  /* remapping takes an axis past 60% */
#define CAPTURE_GAP_NS 300000000ull

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gp;     /* a gamepad SDL has a mapping for, or */
    SDL_Joystick *joy;   /* a joystick without one (an arcade stick, say) */
    int player;
    PsiFamily family;
    uint64_t used;       /* when it was last used (PsiInput.serial) */
} Device;

struct PsiInput {
    PsiOptions o;
    const PsiLayout *lay;
    int nslots;
    char name[40];
    Bind bind[PSI_PLAYERS][PSI_MAX_SLOTS][MAX_BIND];
    uint32_t mask[PSI_PLAYERS][PSI_MAX_SLOTS];
    PsiSwapLatch latch[PSI_PLAYERS];
    int swap[PSI_PLAYERS];
    int analog[PSI_PLAYERS];
    int socd;            /* 1: left+right neutral, up+down up */
    int rumble;
    int forced;          /* -1: auto */
    PsiFamily last;
    uint64_t serial;     /* uses so far, for which device each player used last */
    uint64_t key_used[PSI_PLAYERS];
    int last_player;
    Device dev[MAX_DEV];
    int ndev;
    bool keys[SDL_SCANCODE_COUNT];
    int keys_from_sdl;
    uint32_t touch[PSI_PLAYERS];
    /* remap: remap_player < 0 when nothing is captured */
    int remap_player, remap_slot, remap_result;
    int remap_seq, remap_step;
    uint64_t remap_at;
    char prompt[200];
};

/* ------------------------------------------------------------------ log and store */

static void say(const PsiInput *in, const char *fmt, ...)
{
    char line[600];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (in->o.log) {
        in->o.log(in->o.log_user, line);
    } else {
        fprintf(stderr, "%s: %s\n", in->name, line);
    }
}

static const char *store_get(const PsiInput *in, const char *key)
{
    return in->o.store.get ? in->o.store.get(in->o.store.ctx, key) : NULL;
}

static const char *store_get_or(const PsiInput *in, const char *key, const char *fallback)
{
    const char *v = store_get(in, key);

    return v ? v : fallback;
}

static void store_set(PsiInput *in, const char *key, const char *value)
{
    if (in->o.store.set) {
        in->o.store.set(in->o.store.ctx, key, value);
    }
}

static void store_save(PsiInput *in)
{
    if (in->o.store.save && in->o.store.save(in->o.store.ctx) != 0) {
        say(in, "cannot save the settings");
    }
}

/* ------------------------------------------------------------------ bindings of the layout's slots */

static void slot_key(const PsiInput *in, int player, int slot, int old, char *out, size_t cap)
{
    const PsiSlot *s = &in->lay->slots[slot];

    snprintf(out, cap, "p%d.%s", player + 1, old ? s->old_key : s->key);
}

/* "key:Z, pad:south" into a slot's bindings; bad entries are reported */
static void load_binds(const PsiInput *in, Bind *dst, const char *text, const char *key)
{
    char buf[512], *p = buf;
    int n = 0;

    memset(dst, 0, sizeof(Bind) * MAX_BIND);
    snprintf(buf, sizeof(buf), "%s", text);
    while (*p) {
        char *comma = strchr(p, ','), *e;

        if (comma) {
            *comma = 0;
        }
        while (*p == ' ') {
            p++;
        }
        e = p + strlen(p);
        while (e > p && e[-1] == ' ') {
            *--e = 0;
        }
        if (*p && strcmp(p, "none")) {
            if (n < MAX_BIND && bind_parse(p, &dst[n]) == 0) {
                n++;
            } else {
                say(in, "%s: \"%s\" is not a binding (key:<name>, pad:<button or axis+->, "
                        "joy:b<n> / joy:h<n>.up / joy:a<n>+): ignored", key, p);
            }
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
}

static void load_default(PsiInput *in, int p, int s, const char *key)
{
    const char *d = in->lay->slots[s].defaults;
    Bind *l = in->bind[p][s];

    load_binds(in, l, d ? d : "none", key);
    if (p > 0) {
        /* the other players have no keys by default: the keyboard is player 1's */
        int i, n = 0;

        for (i = 0; i < MAX_BIND; i++) {
            if (l[i].kind != B_NONE && l[i].kind != B_KEY) {
                l[n++] = l[i];
            }
        }
        memset(l + n, 0, sizeof(Bind) * (size_t)(MAX_BIND - n));
    }
}

static void bind_text_of(const Bind *list, char *val, size_t cap)
{
    char one[64];
    int i;

    val[0] = 0;
    for (i = 0; i < MAX_BIND; i++) {
        if (list[i].kind == B_NONE) {
            continue;
        }
        bind_format(&list[i], one, sizeof(one));
        if (val[0]) {
            strncat(val, ", ", cap - strlen(val) - 1);
        }
        strncat(val, one, cap - strlen(val) - 1);
    }
}

static void save_binds(PsiInput *in, int player, int slot)
{
    char key[64], val[512];

    bind_text_of(in->bind[player][slot], val, sizeof(val));
    slot_key(in, player, slot, 0, key, sizeof(key));
    store_set(in, key, val[0] ? val : "none");
}

/* ------------------------------------------------------------------ devices */

int psi_on_steam_deck(void)
{
    const char *e = SDL_getenv("SteamDeck");

    if (e && !strcmp(e, "1")) {
        return 1;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    {
        FILE *f = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
        char name[64] = "";

        if (f) {
            if (!fgets(name, sizeof(name), f)) {
                name[0] = 0;
            }
            fclose(f);
        }
        return !strncmp(name, "Jupiter", 7) || !strncmp(name, "Galileo", 7);
    }
#else
    return 0;
#endif
}

PsiFamily psi_gamepad_family(SDL_GamepadType type, Uint16 vendor, Uint16 product, int on_deck)
{
    const char *name = SDL_GetGamepadStringForType(type);

    /* newer SDLs have a Steam type (SDL_GAMEPAD_TYPE_STEAM); by name, so older ones build too */
    if (name && !strcmp(name, "steam")) {
        return PSI_FAMILY_STEAM;
    }
    if (vendor == 0x28de) {
        /* Valve: the Steam Deck's controls, the Steam Controller (wired, its dongle, Bluetooth) */
        if (product == 0x1205 || product == 0x1102 || product == 0x1142 || product == 0x1106) {
            return PSI_FAMILY_STEAM;
        }
        /* Steam Input's virtual gamepad on a Deck: the Deck's own controls, shown to the game as an Xbox pad */
        if (product == 0x11ff && on_deck && type != SDL_GAMEPAD_TYPE_PS3 && type != SDL_GAMEPAD_TYPE_PS4 &&
            type != SDL_GAMEPAD_TYPE_PS5) {
            return PSI_FAMILY_STEAM;
        }
    }
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5:
        return PSI_FAMILY_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return PSI_FAMILY_XBOX;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return PSI_FAMILY_NINTENDO;
    default:
        return PSI_FAMILY_OTHER;
    }
}

static const char *dev_name(const Device *d)
{
    return d->gp ? SDL_GetGamepadName(d->gp) : SDL_GetJoystickName(d->joy);
}

static void dev_used(PsiInput *in, Device *d)
{
    d->used = ++in->serial;
    in->last_player = d->player;
}

/* A key counts for the players it is bound for. */
static void key_used(PsiInput *in, SDL_Scancode sc)
{
    int p, b, i;

    for (p = 0; p < PSI_PLAYERS; p++) {
        for (b = 0; b < in->nslots; b++) {
            for (i = 0; i < MAX_BIND; i++) {
                if (in->bind[p][b][i].kind == B_KEY && in->bind[p][b][i].code == (int16_t)sc) {
                    in->key_used[p] = ++in->serial;
                    in->last_player = p;
                    b = in->nslots;
                    break;
                }
            }
        }
    }
}

static Device *find_dev(PsiInput *in, SDL_JoystickID id)
{
    int i;

    for (i = 0; i < in->ndev; i++) {
        if (in->dev[i].id == id) {
            return &in->dev[i];
        }
    }
    return NULL;
}

static int pick_player(PsiInput *in, SDL_JoystickID id)
{
    char guid[64], key[32];
    int p, i, used[PSI_PLAYERS] = { 0 };

    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), guid, sizeof(guid));
    for (p = 0; p < PSI_PLAYERS; p++) {
        snprintf(key, sizeof(key), "p%d.device", p + 1);
        if (!SDL_strcasecmp(store_get_or(in, key, ""), guid)) {
            return p;
        }
    }
    for (i = 0; i < in->ndev; i++) {
        used[in->dev[i].player] = 1;
    }
    for (p = 0; p < PSI_PLAYERS; p++) {
        if (!used[p]) {
            return p;
        }
    }
    return 0;
}

static void add_device(PsiInput *in, SDL_JoystickID id, int gamepad)
{
    Device *d;

    if (find_dev(in, id) || in->ndev >= MAX_DEV || (in->o.only_virtual && !SDL_IsJoystickVirtual(id))) {
        return;
    }
    d = &in->dev[in->ndev];
    memset(d, 0, sizeof(*d));
    if (gamepad) {
        d->gp = SDL_OpenGamepad(id);
    } else {
        d->joy = SDL_OpenJoystick(id);
    }
    if (!d->gp && !d->joy) {
        say(in, "cannot open a controller: %s", SDL_GetError());
        return;
    }
    d->id = id;
    d->player = pick_player(in, id);
    d->family = gamepad ? psi_gamepad_family(SDL_GetGamepadType(d->gp), SDL_GetGamepadVendor(d->gp),
                                             SDL_GetGamepadProduct(d->gp), psi_on_steam_deck())
                        : PSI_FAMILY_OTHER;
    in->ndev++;
    if (in->ndev == 1 && in->last == PSI_FAMILY_KEYBOARD) {
        in->last = d->family;
    }
    say(in, "player %d: %s (%s, %s, %04x:%04x)", d->player + 1, dev_name(d), d->gp ? "gamepad" : "joystick",
        psi_family_name(d->family), SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
}

static void remove_device(PsiInput *in, SDL_JoystickID id)
{
    Device *d = find_dev(in, id);

    if (!d) {
        return;
    }
    say(in, "player %d: %s disconnected", d->player + 1, dev_name(d));
    if (d->gp) {
        SDL_CloseGamepad(d->gp);
    } else {
        SDL_CloseJoystick(d->joy);
    }
    *d = in->dev[--in->ndev];
}

/* ------------------------------------------------------------------ the layer */

PsiInput *psi_new(const PsiOptions *opt)
{
    PsiInput *in;
    const PsiLayout *lay;
    SDL_JoystickID *ids;
    const char *v;
    int p, b, n, i;

    if (!opt || !(lay = opt->layout) || !lay->slots || lay->nslots < 1 || lay->nslots > PSI_MAX_SLOTS) {
        return NULL;
    }
    for (b = 0; b < lay->nslots; b++) {
        if (!lay->slots[b].name || !lay->slots[b].key || !*lay->slots[b].key) {
            return NULL;
        }
    }
    for (i = 0; i < lay->nremap; i++) {
        if (!lay->remap_order || lay->remap_order[i] < 0 || lay->remap_order[i] >= lay->nslots) {
            return NULL;
        }
    }
    in = calloc(1, sizeof(PsiInput));
    if (!in) {
        return NULL;
    }
    in->o = *opt;
    in->lay = lay;
    in->nslots = lay->nslots;
    snprintf(in->name, sizeof(in->name), "%s", opt->name && *opt->name ? opt->name : "input");
    in->remap_player = -1;
    in->remap_slot = -1;
    in->last = PSI_FAMILY_KEYBOARD;
    for (p = 0; p < PSI_PLAYERS; p++) {
        char key[64];

        for (b = 0; b < in->nslots; b++) {
            in->mask[p][b] = lay->slots[b].mask;
            slot_key(in, p, b, 0, key, sizeof(key));
            v = store_get(in, key);
            if (!v && lay->slots[b].old_key) {
                char old[64];

                slot_key(in, p, b, 1, old, sizeof(old));
                v = store_get(in, old);   /* written under an earlier name */
            }
            if (v) {
                load_binds(in, in->bind[p][b], v, key);
            } else {
                load_default(in, p, b, key);
            }
        }
        snprintf(key, sizeof(key), "p%d.analog", p + 1);
        in->analog[p] = !strcmp(store_get_or(in, key, "off"), "on");
        psi_swap_reset(&in->latch[p]);
    }
    in->socd = strcmp(store_get_or(in, "socd", "hitbox"), "off") != 0;
    in->rumble = strcmp(store_get_or(in, "rumble", "on"), "off") != 0;
    v = store_get_or(in, "prompts", "auto");
    in->forced = psi_family_parse(v);
    if (in->forced < 0 && strcmp(v, "auto")) {
        say(in, "prompts = %s: not auto, playstation, xbox, steam, nintendo, other or keyboard; using auto", v);
    }
    /* the devices already there: their "added" events may have gone by */
    ids = SDL_GetGamepads(&n);
    for (i = 0; ids && i < n; i++) {
        add_device(in, ids[i], 1);
    }
    SDL_free(ids);
    ids = SDL_GetJoysticks(&n);
    for (i = 0; ids && i < n; i++) {
        if (!SDL_IsGamepad(ids[i])) {
            add_device(in, ids[i], 0);
        }
    }
    SDL_free(ids);
    return in;
}

void psi_free(PsiInput *in)
{
    if (!in) {
        return;
    }
    while (in->ndev) {
        remove_device(in, in->dev[0].id);
    }
    free(in);
}

void psi_keys_from_sdl(PsiInput *in, int on)
{
    in->keys_from_sdl = on != 0;
}

void psi_set_touch(PsiInput *in, int player, uint32_t held)
{
    in->touch[player & 1] = held;
}

void psi_set_slot_mask(PsiInput *in, int player, int slot, uint32_t mask)
{
    if (slot >= 0 && slot < in->nslots) {
        in->mask[player & 1][slot] = mask;
    }
}

void psi_set_swap(PsiInput *in, int player, int on)
{
    in->swap[player & 1] = on != 0;
}

/* ------------------------------------------------------------------ remapping */

static int remap_active(const PsiInput *in)
{
    return in->remap_player >= 0;
}

static int remap_current(const PsiInput *in)
{
    if (in->remap_seq) {
        return in->lay->remap_order ? in->lay->remap_order[in->remap_step] : in->remap_step;
    }
    return in->remap_slot;
}

static int remap_count(const PsiInput *in)
{
    return in->lay->remap_order ? in->lay->nremap : in->nslots;
}

static void remap_finish(PsiInput *in, int result)
{
    in->remap_player = -1;
    in->remap_slot = -1;
    in->remap_seq = 0;
    in->prompt[0] = 0;
    in->remap_result = result;
    if (result == 1) {
        store_save(in);
    }
}

/* the prompt for the run's current slot, or the end of the run */
static void remap_prompt(PsiInput *in)
{
    const Bind *list;
    char now[96] = "", one[48];
    int i, slot;

    if (in->remap_step >= remap_count(in)) {
        int b, player = in->remap_player;

        for (b = 0; b < in->nslots; b++) {
            save_binds(in, player, b);
        }
        say(in, "player %d's buttons saved", player + 1);
        remap_finish(in, 1);
        return;
    }
    slot = remap_current(in);
    list = in->bind[in->remap_player][slot];
    /* what it is bound to now, so a player sees the extras' bindings too */
    for (i = 0; i < MAX_BIND && list[i].kind != B_NONE; i++) {
        bind_format(&list[i], one, sizeof(one));
        if (strlen(now) + strlen(one) + 3 >= sizeof(now)) {
            break;
        }
        strcat(now, now[0] ? ", " : "");
        strcat(now, one);
    }
    snprintf(in->prompt, sizeof(in->prompt), "Player %d: press %s (now %s; Esc keeps it)", in->remap_player + 1,
             in->lay->slots[slot].name, now[0] ? now : "none");
    say(in, "%s", in->prompt);
}

void psi_remap_begin(PsiInput *in, int player, int slot)
{
    if (slot < 0 || slot >= in->nslots) {
        return;
    }
    in->remap_player = player & 1;
    in->remap_slot = slot;
    in->remap_seq = 0;
    in->remap_result = 0;
    in->remap_at = SDL_GetTicksNS();
    in->prompt[0] = 0;
}

void psi_remap_start(PsiInput *in, int player)
{
    in->remap_player = player & 1;
    in->remap_slot = -1;
    in->remap_seq = 1;
    in->remap_step = 0;
    in->remap_result = 0;
    in->remap_at = SDL_GetTicksNS();
    remap_prompt(in);
}

void psi_remap_cancel(PsiInput *in)
{
    if (remap_active(in)) {
        remap_finish(in, 2);
    }
}

int psi_remap_slot(const PsiInput *in)
{
    return remap_active(in) ? remap_current(in) : -1;
}

const char *psi_remap_prompt(const PsiInput *in)
{
    return remap_active(in) && in->remap_seq ? in->prompt : NULL;
}

int psi_remap_take_result(PsiInput *in)
{
    int r = in->remap_result;

    in->remap_result = 0;
    return r;
}

/* A capture: b replaces the current slot's bindings of its class. */
static void remap_take(PsiInput *in, const Bind *b)
{
    int slot = remap_current(in);
    Bind *list = in->bind[in->remap_player][slot];
    Bind keep[MAX_BIND];
    int i, n = 0;
    uint64_t now = SDL_GetTicksNS();

    if (now - in->remap_at < CAPTURE_GAP_NS) {
        return;   /* the press that started it, or a stick still coming back */
    }
    for (i = 0; i < MAX_BIND; i++) {
        if (list[i].kind != B_NONE && bind_class(&list[i]) != bind_class(b)) {
            keep[n++] = list[i];
        }
    }
    keep[n < MAX_BIND ? n++ : MAX_BIND - 1] = *b;
    memset(list, 0, sizeof(Bind) * MAX_BIND);
    memcpy(list, keep, sizeof(Bind) * (size_t)n);
    in->remap_at = now;
    save_binds(in, in->remap_player, slot);
    {
        char one[64];

        bind_format(b, one, sizeof(one));
        say(in, "player %d's %s is now %s", in->remap_player + 1, in->lay->slots[slot].name, one);
    }
    if (in->remap_seq) {
        in->remap_step++;
        remap_prompt(in);
    } else {
        remap_finish(in, 1);
    }
}

static void remap_escape(PsiInput *in)
{
    if (in->remap_seq) {
        in->remap_step++;
        remap_prompt(in);
    } else {
        remap_finish(in, 2);
    }
}

void psi_reset_player(PsiInput *in, int player)
{
    int b;
    char key[64];

    player &= 1;
    for (b = 0; b < in->nslots; b++) {
        slot_key(in, player, b, 0, key, sizeof(key));
        load_default(in, player, b, key);
        save_binds(in, player, b);
    }
    store_save(in);
}

/* ------------------------------------------------------------------ events */

int psi_event(PsiInput *in, const SDL_Event *e)
{
    Device *d;
    Bind b;

    memset(&b, 0, sizeof(b));
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e->key.scancode < SDL_SCANCODE_COUNT) {
            in->keys[e->key.scancode] = e->type == SDL_EVENT_KEY_DOWN;
        }
        if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
            in->last = PSI_FAMILY_KEYBOARD;
            key_used(in, e->key.scancode);
            if (remap_active(in)) {
                if (e->key.scancode == SDL_SCANCODE_ESCAPE) {
                    remap_escape(in);
                } else {
                    b.kind = B_KEY;
                    b.code = (int16_t)e->key.scancode;
                    remap_take(in, &b);
                }
                return 1;
            }
        }
        return 0;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        memset(in->keys, 0, sizeof(in->keys));
        return 0;
    case SDL_EVENT_GAMEPAD_ADDED:
        add_device(in, e->gdevice.which, 1);
        return 0;
    case SDL_EVENT_JOYSTICK_ADDED:
        if (!SDL_IsGamepad(e->jdevice.which)) {
            add_device(in, e->jdevice.which, 0);
        }
        return 0;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        remove_device(in, e->type == SDL_EVENT_GAMEPAD_REMOVED ? e->gdevice.which : e->jdevice.which);
        return 0;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if ((d = find_dev(in, e->gbutton.which)) != NULL && d->gp) {
            in->last = d->family;
            dev_used(in, d);
            if (remap_active(in)) {
                b.kind = B_PAD_BUTTON;
                b.code = (int16_t)e->gbutton.button;
                remap_take(in, &b);
                return 1;
            }
        }
        return 0;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if ((d = find_dev(in, e->gaxis.which)) != NULL && d->gp && (e->gaxis.value > STICK_ON || e->gaxis.value < -STICK_ON)) {
            in->last = d->family;
            dev_used(in, d);
            if (remap_active(in) && (e->gaxis.value > CAPTURE_AXIS || e->gaxis.value < -CAPTURE_AXIS)) {
                b.kind = B_PAD_AXIS;
                b.code = (int16_t)e->gaxis.axis;
                b.dir = e->gaxis.value > 0 ? 1 : -1;
                remap_take(in, &b);
                return 1;
            }
        }
        return 0;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        /* the same events come for a gamepad's joystick: only plain joysticks count here */
        d = find_dev(in, e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ? e->jbutton.which
                         : e->type == SDL_EVENT_JOYSTICK_HAT_MOTION ? e->jhat.which : e->jaxis.which);
        if (!d || !d->joy) {
            return 0;
        }
        if (e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
            b.kind = B_JOY_BUTTON;
            b.code = (int16_t)e->jbutton.button;
        } else if (e->type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
            int h = e->jhat.value;

            if (h == SDL_HAT_CENTERED) {
                return 0;
            }
            b.kind = B_JOY_HAT;
            b.code = (int16_t)e->jhat.hat;
            b.dir = (int8_t)((h & SDL_HAT_UP) ? SDL_HAT_UP : (h & SDL_HAT_DOWN) ? SDL_HAT_DOWN
                             : (h & SDL_HAT_LEFT) ? SDL_HAT_LEFT : SDL_HAT_RIGHT);
        } else {
            if (e->jaxis.value <= CAPTURE_AXIS && e->jaxis.value >= -CAPTURE_AXIS) {
                return 0;
            }
            b.kind = B_JOY_AXIS;
            b.code = (int16_t)e->jaxis.axis;
            b.dir = e->jaxis.value > 0 ? 1 : -1;
        }
        in->last = d->family;
        dev_used(in, d);
        if (remap_active(in)) {
            remap_take(in, &b);
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ reading the pads */

static int key_down(const PsiInput *in, int sc)
{
    if (in->keys_from_sdl) {
        int n = 0;
        const bool *k = SDL_GetKeyboardState(&n);

        return k && sc < n && k[sc];
    }
    return in->keys[sc];
}

static int bind_on(const PsiInput *in, int player, const Bind *b)
{
    int i;

    if (b->kind == B_KEY) {
        return key_down(in, b->code);
    }
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];

        if (d->player != player) {
            continue;
        }
        switch (b->kind) {
        case B_PAD_BUTTON:
            if (d->gp && SDL_GetGamepadButton(d->gp, (SDL_GamepadButton)b->code)) {
                return 1;
            }
            break;
        case B_PAD_AXIS:
            if (d->gp) {
                int v = SDL_GetGamepadAxis(d->gp, (SDL_GamepadAxis)b->code) * b->dir;
                int on = b->code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || b->code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
                             ? TRIGGER_ON : STICK_ON;

                if (v > on) {
                    return 1;
                }
            }
            break;
        case B_JOY_BUTTON:
            if (d->joy && SDL_GetJoystickButton(d->joy, b->code)) {
                return 1;
            }
            break;
        case B_JOY_HAT:
            if (d->joy && (SDL_GetJoystickHat(d->joy, b->code) & b->dir)) {
                return 1;
            }
            break;
        case B_JOY_AXIS:
            if (d->joy && SDL_GetJoystickAxis(d->joy, b->code) * b->dir > STICK_ON) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

static uint8_t axis_byte(Sint16 v)
{
    return (uint8_t)(((int)v + 32768) >> 8);
}

static void read_pads(PsiInput *in, PsiPad pads[PSI_PLAYERS], int clean)
{
    const PsiLayout *lay = in->lay;
    int p, b, i;

    for (p = 0; p < PSI_PLAYERS; p++) {
        PsiPad *o = &pads[p];
        const Device *stick = NULL;

        psi_pad_clear(o);
        o->analog = in->analog[p] != 0;
        if (clean && remap_active(in)) {
            continue;
        }
        for (b = 0; b < in->nslots; b++) {
            for (i = 0; i < MAX_BIND && in->bind[p][b][i].kind != B_NONE; i++) {
                if (bind_on(in, p, &in->bind[p][b][i])) {
                    o->buttons |= in->mask[p][b];
                    break;
                }
            }
        }
        o->buttons |= in->touch[p];
        if (clean) {
            if (lay->swap_a && lay->swap_b) {
                o->buttons = psi_swap_apply(&in->latch[p], o->buttons, lay->swap_a, lay->swap_b, in->swap[p]);
            }
            if (in->socd) {
                o->buttons = psi_socd_clean(o->buttons, lay->up, lay->down, lay->left, lay->right);
            }
        }
        if (!o->analog) {
            continue;
        }
        for (i = 0; i < in->ndev && !stick; i++) {
            if (in->dev[i].player == p) {
                stick = &in->dev[i];
            }
        }
        if (stick && stick->gp) {
            o->lx = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_LEFTX));
            o->ly = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_LEFTY));
            o->rx = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_RIGHTX));
            o->ry = axis_byte(SDL_GetGamepadAxis(stick->gp, SDL_GAMEPAD_AXIS_RIGHTY));
        } else if (stick && SDL_GetNumJoystickAxes(stick->joy) >= 2) {
            o->lx = axis_byte(SDL_GetJoystickAxis(stick->joy, 0));
            o->ly = axis_byte(SDL_GetJoystickAxis(stick->joy, 1));
        }
        /* keys, d-pads and hats steer the left stick while it is centred */
        psi_steer_stick(o, lay->up, lay->down, lay->left, lay->right);
    }
}

void psi_pads(PsiInput *in, PsiPad pads[PSI_PLAYERS])
{
    read_pads(in, pads, 1);
}

void psi_raw(PsiInput *in, PsiPad pads[PSI_PLAYERS])
{
    read_pads(in, pads, 0);
}

/* ------------------------------------------------------------------ settings of the layer */

int psi_analog(const PsiInput *in, int player)
{
    return in->analog[player & 1];
}

void psi_set_analog(PsiInput *in, int player, int on)
{
    char key[32];

    in->analog[player & 1] = on != 0;
    snprintf(key, sizeof(key), "p%d.analog", (player & 1) + 1);
    store_set(in, key, on ? "on" : "off");
}

void psi_toggle_analog(PsiInput *in, int player)
{
    psi_set_analog(in, player, !in->analog[player & 1]);
    say(in, "player %d's pad is %s", (player & 1) + 1, in->analog[player & 1] ? "analog" : "digital");
}

void psi_swap_players(PsiInput *in)
{
    int i;

    for (i = 0; i < in->ndev; i++) {
        in->dev[i].player = (in->dev[i].player + 1) % PSI_PLAYERS;
    }
    psi_describe(in, NULL);
}

void psi_pin_device(PsiInput *in, int player, const char *guid)
{
    char key[32];
    int i;

    player &= 1;
    snprintf(key, sizeof(key), "p%d.device", player + 1);
    store_set(in, key, guid);
    for (i = 0; i < in->ndev; i++) {
        char g[64];

        SDL_GUIDToString(SDL_GetJoystickGUIDForID(in->dev[i].id), g, sizeof(g));
        if (!SDL_strcasecmp(g, guid)) {
            in->dev[i].player = player;
        }
    }
}

/* ------------------------------------------------------------------ what a person reads */

void psi_binding_text(const PsiInput *in, int player, int slot, char *out, size_t cap)
{
    const Bind *list;
    char one[48];
    size_t len = 0;
    int i, shown = 0;

    if (!cap) {
        return;
    }
    out[0] = 0;
    if (slot < 0 || slot >= in->nslots) {
        return;
    }
    list = in->bind[player & 1][slot];
    for (i = 0; i < MAX_BIND && list[i].kind != B_NONE; i++) {
        /* the stick halves that copy the d-pad are not news to anyone */
        if (list[i].kind == B_JOY_HAT || list[i].kind == B_JOY_AXIS ||
            (list[i].kind == B_PAD_AXIS && list[i].code != SDL_GAMEPAD_AXIS_LEFT_TRIGGER &&
             list[i].code != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER && shown)) {
            continue;
        }
        if (list[i].kind == B_PAD_BUTTON && list[i].code >= SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 &&
            list[i].code <= SDL_GAMEPAD_BUTTON_LEFT_PADDLE2) {
            continue;
        }
        bind_describe(&list[i], one, sizeof(one));
        len += (size_t)snprintf(out + len, len < cap ? cap - len : 0, "%s%s", shown ? ", " : "", one);
        shown++;
        if (len >= cap) {
            break;
        }
    }
    if (!shown) {
        snprintf(out, cap, "not set");
    }
}

void psi_player_devices(const PsiInput *in, int player, char *out, size_t cap)
{
    size_t len = 0;
    int i, n = 0;

    if (!cap) {
        return;
    }
    out[0] = 0;
    player &= 1;
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];

        if (d->player != player) {
            continue;
        }
        len += (size_t)snprintf(out + len, len < cap ? cap - len : 0, "%s%s", n ? ", " : "", dev_name(d));
        n++;
        if (len >= cap) {
            return;
        }
    }
    if (player == 0) {
        snprintf(out + len, len < cap ? cap - len : 0, "%skeyboard", n ? ", " : "");
    } else if (!n) {
        snprintf(out, cap, "none");
    }
}

int psi_device_count(const PsiInput *in)
{
    return in->ndev;
}

void psi_describe(const PsiInput *in, FILE *out)
{
    int i;

    if (!in->ndev) {
        if (out) {
            fprintf(out, "%s: no controllers: player 1 is the keyboard\n", in->name);
        } else {
            say(in, "no controllers: player 1 is the keyboard");
        }
    }
    for (i = 0; i < in->ndev; i++) {
        const Device *d = &in->dev[i];
        char guid[64], line[400];

        SDL_GUIDToString(SDL_GetJoystickGUIDForID(d->id), guid, sizeof(guid));
        snprintf(line, sizeof(line), "player %d: %s (%s, %s, %04x:%04x, GUID %s)", d->player + 1, dev_name(d),
                 d->gp ? "gamepad" : "joystick", psi_family_name(d->family), SDL_GetJoystickVendorForID(d->id),
                 SDL_GetJoystickProductForID(d->id), guid);
        if (out) {
            fprintf(out, "%s: %s\n", in->name, line);
        } else {
            say(in, "%s", line);
        }
    }
}

/* ------------------------------------------------------------------ family and prompts */

PsiFamily psi_family(const PsiInput *in)
{
    return in->forced >= 0 ? (PsiFamily)in->forced : in->last;
}

void psi_force_family(PsiInput *in, int family)
{
    in->forced = family >= 0 && family < PSI_FAMILY_COUNT ? family : -1;
}

int psi_last_player(const PsiInput *in)
{
    return in->last_player;
}

static char label_letter(SDL_GamepadButtonLabel l)
{
    switch (l) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return 'A';
    case SDL_GAMEPAD_BUTTON_LABEL_B: return 'B';
    case SDL_GAMEPAD_BUTTON_LABEL_X: return 'X';
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return 'Y';
    default: return 0;        /* a PlayStation shape, or no label */
    }
}

/* The first binding of a kind for a slot, or NULL. */
static const Bind *bound(const PsiInput *in, int player, int slot, int kind)
{
    int i;

    for (i = 0; i < MAX_BIND && in->bind[player][slot][i].kind != B_NONE; i++) {
        if (in->bind[player][slot][i].kind == kind) {
            return &in->bind[player][slot][i];
        }
    }
    return NULL;
}

static char key_letter(const Bind *b)
{
    SDL_Keycode k;

    if (!b) {
        return 0;
    }
    k = SDL_GetKeyFromScancode((SDL_Scancode)b->code, SDL_KMOD_NONE, false);
    if (k >= 'a' && k <= 'z') {
        return (char)(k - 'a' + 'A');
    }
    return k >= '0' && k <= '9' ? (char)k : 0;
}

void psi_prompt_letters(const PsiInput *in, int player, const int *slots, int n, char *out)
{
    const Device *d = NULL;
    SDL_GamepadType forced_type = SDL_GAMEPAD_TYPE_UNKNOWN;
    int keyboard = 0, i, k;

    player &= 1;
    if (n > 8) {
        n = 8;
    }
    memset(out, 0, (size_t)(n > 0 ? n : 0));
    if (in->forced >= 0) {
        switch ((PsiFamily)in->forced) {
        case PSI_FAMILY_XBOX:
        case PSI_FAMILY_STEAM:
            forced_type = SDL_GAMEPAD_TYPE_XBOXONE;    /* the Deck's and the Steam Controller's letters are Xbox's */
            break;
        case PSI_FAMILY_NINTENDO:
            forced_type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
            break;
        case PSI_FAMILY_KEYBOARD:
            keyboard = 1;
            break;
        default:
            return;
        }
    } else {
        /* the player's device used last; before any, the first they have, else the keyboard */
        for (i = 0; i < in->ndev; i++) {
            if (in->dev[i].player == player && (!d || in->dev[i].used > d->used)) {
                d = &in->dev[i];
            }
        }
        if (!d || in->key_used[player] > d->used) {
            keyboard = 1;
        }
    }
    for (k = 0; k < n; k++) {
        const Bind *b;

        if (slots[k] < 0 || slots[k] >= in->nslots) {
            continue;
        }
        if (keyboard) {
            out[k] = key_letter(bound(in, player, slots[k], B_KEY));
        } else if ((b = bound(in, player, slots[k], B_PAD_BUTTON)) != NULL) {
            SDL_GamepadButton gb = (SDL_GamepadButton)b->code;

            if (forced_type != SDL_GAMEPAD_TYPE_UNKNOWN) {
                out[k] = label_letter(SDL_GetGamepadButtonLabelForType(forced_type, gb));
            } else if (d->gp) {
                out[k] = label_letter(SDL_GetGamepadButtonLabel(d->gp, gb));
            }
        }
    }
}

/* ------------------------------------------------------------------ rumble */

void psi_rumble(PsiInput *in, int player, Uint16 small_motor, Uint16 large_motor, Uint32 ms)
{
    int i;

    player &= 1;
    if (!in->rumble) {
        return;
    }
    for (i = 0; i < in->ndev; i++) {
        Device *d = &in->dev[i];

        if (d->gp && d->player == player) {
            SDL_RumbleGamepad(d->gp, large_motor, small_motor, ms);
        }
    }
}
