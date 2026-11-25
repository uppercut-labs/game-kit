/* psi-test: the input layer with SDL's virtual controllers. No window, GPU or real controller.
 *
 *   psi-test [<scratch dir>]
 *
 * Two made-up ports drive it. Port A has the sixteen PlayStation buttons in one bit order and nothing else. Port B has
 * the same sixteen in another bit order, other default keys, four extra slots whose bits it sets while it runs
 * (macros) and a pair of buttons that swap in menus: what the layout data is for. Checked: every button by position on
 * a DualSense, a DualShock 4, an Xbox Series pad, a Steam Deck's controls and a Switch Pro, with no setup; the
 * keyboard beside the pads; players assigned as devices connect; hot-plug; swapping and pinning; SOCD; analog mode; an
 * arcade stick SDL has no mapping for; the on-screen controller; remap capture of one button and of a run, saved as
 * text and read back, including a key whose name holds a comma; the family and prompt letters; rumble; the swap; extras.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "psi/psi.h"

static int failures, checks;

#define CHECK(cond, ...)                                 \
    do {                                                 \
        checks++;                                        \
        if (!(cond)) {                                   \
            failures++;                                  \
            fprintf(stderr, "FAIL line %d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                \
            fputc('\n', stderr);                         \
        }                                                \
    } while (0)

/* ------------------------------------------------------------------ a store in memory */

typedef struct {
    char key[96][64];
    char val[96][512];
    int n, saves;
} Mem;

static const char *mem_get(void *ctx, const char *key)
{
    Mem *m = ctx;
    int i;

    for (i = 0; i < m->n; i++) {
        if (!strcmp(m->key[i], key)) {
            return m->val[i];
        }
    }
    return NULL;
}

static void mem_set(void *ctx, const char *key, const char *value)
{
    Mem *m = ctx;
    int i;

    for (i = 0; i < m->n; i++) {
        if (!strcmp(m->key[i], key)) {
            snprintf(m->val[i], sizeof(m->val[i]), "%s", value);
            return;
        }
    }
    if (m->n < 96) {
        snprintf(m->key[m->n], sizeof(m->key[0]), "%s", key);
        snprintf(m->val[m->n], sizeof(m->val[0]), "%s", value);
        m->n++;
    }
}

static int mem_save(void *ctx)
{
    ((Mem *)ctx)->saves++;
    return 0;
}

static const char *mget(Mem *m, const char *key)
{
    const char *v = mem_get(m, key);

    return v ? v : "(none)";
}

/* ------------------------------------------------------------------ the made-up ports */

/* port A: PlayStation 1 order */
static const uint32_t A_BITS[PSI_PS_COUNT] = {
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
};
enum {
    A_SELECT = 0x0001, A_L3 = 0x0002, A_R3 = 0x0004, A_START = 0x0008, A_UP = 0x0010, A_RIGHT = 0x0020,
    A_DOWN = 0x0040, A_LEFT = 0x0080, A_L2 = 0x0100, A_R2 = 0x0200, A_L1 = 0x0400, A_R1 = 0x0800,
    A_TRIANGLE = 0x1000, A_CIRCLE = 0x2000, A_X = 0x4000, A_SQUARE = 0x8000,
};
static PsiSlot a_slots[PSI_PS_COUNT];
static PsiLayout a_layout;

/* port B: PlayStation 2 order, its own keys, four macros, X and Circle swap in menus */
static const uint32_t B_BITS[PSI_PS_COUNT] = {
    0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000,
    0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
};
enum {
    B_L2 = 0x0001, B_R2 = 0x0002, B_L1 = 0x0004, B_R1 = 0x0008, B_TRIANGLE = 0x0010, B_CIRCLE = 0x0020,
    B_CROSS = 0x0040, B_SQUARE = 0x0080, B_SELECT = 0x0100, B_START = 0x0800, B_UP = 0x1000, B_RIGHT = 0x2000,
    B_DOWN = 0x4000, B_LEFT = 0x8000,
};
#define B_EXTRA 16
static PsiSlot b_slots[PSI_PS_COUNT + 4];
static int b_order[PSI_PS_COUNT + 4];
static PsiLayout b_layout;

static void build_ports(void)
{
    int i;

    psi_playstation_slots(a_slots, A_BITS);
    a_layout.slots = a_slots;
    a_layout.nslots = PSI_PS_COUNT;
    a_layout.up = A_UP, a_layout.down = A_DOWN, a_layout.left = A_LEFT, a_layout.right = A_RIGHT;

    psi_playstation_slots(b_slots, B_BITS);
    b_slots[PSI_PS_TRIANGLE].defaults = "key:S, pad:north, joy:b3";
    b_slots[PSI_PS_CIRCLE].defaults = "key:X, pad:east, joy:b2";
    b_slots[PSI_PS_X].defaults = "key:Z, pad:south, joy:b1";
    b_slots[PSI_PS_SQUARE].defaults = "key:A, pad:west, joy:b0";
    b_slots[PSI_PS_L1].defaults = "key:Q, pad:leftshoulder, joy:b4";
    b_slots[PSI_PS_R1].defaults = "key:W, pad:rightshoulder, joy:b5";
    b_slots[PSI_PS_L2].defaults = "key:E, pad:lefttrigger+, joy:b6";
    b_slots[PSI_PS_R2].defaults = "key:R, pad:righttrigger+, joy:b7";
    b_slots[PSI_PS_L3].defaults = "none";
    b_slots[PSI_PS_R3].defaults = "none";
    b_slots[B_EXTRA + 0] = (PsiSlot){ "P AND K", "p_and_k", NULL, 0, "key:D, pad:leftstick, pad:l4" };
    b_slots[B_EXTRA + 1] = (PsiSlot){ "S AND HS", "s_and_hs", NULL, 0, "key:F, pad:rightstick, pad:r4" };
    b_slots[B_EXTRA + 2] = (PsiSlot){ "P K S", "p_k_s", NULL, 0, "key:C, pad:touchpad, pad:l5" };
    b_slots[B_EXTRA + 3] = (PsiSlot){ "P K S HS", "p_k_s_hs", NULL, 0, "key:V, pad:r5" };
    for (i = 0; i < PSI_PS_COUNT; i++) {
        b_order[i] = psi_playstation_remap_order[i];
    }
    for (i = 0; i < 4; i++) {
        b_order[PSI_PS_COUNT + i] = B_EXTRA + i;
    }
    b_layout.slots = b_slots;
    b_layout.nslots = PSI_PS_COUNT + 4;
    b_layout.up = B_UP, b_layout.down = B_DOWN, b_layout.left = B_LEFT, b_layout.right = B_RIGHT;
    b_layout.remap_order = b_order;
    b_layout.nremap = PSI_PS_COUNT + 4;
    b_layout.swap_a = B_CROSS;
    b_layout.swap_b = B_CIRCLE;
}

/* the macros' bits, from "the game's own keys": which buttons P, K, S and HS are */
typedef struct {
    uint32_t punch, kick, slash, hslash;
} GameKeys;

static void set_macros(PsiInput *in, int player, const GameKeys *k)
{
    psi_set_slot_mask(in, player, B_EXTRA + 0, k->punch | k->kick);
    psi_set_slot_mask(in, player, B_EXTRA + 1, k->slash | k->hslash);
    psi_set_slot_mask(in, player, B_EXTRA + 2, k->punch | k->kick | k->slash);
    psi_set_slot_mask(in, player, B_EXTRA + 3, k->punch | k->kick | k->slash | k->hslash);
}

static const GameKeys DEFAULT_KEYS = { B_SQUARE, B_CROSS, B_TRIANGLE, B_CIRCLE };

/* ------------------------------------------------------------------ virtual controllers */

typedef struct {
    Uint16 lo, hi;
    int calls;
} Rumble;

static bool SDLCALL on_rumble(void *userdata, Uint16 lo, Uint16 hi)
{
    Rumble *r = userdata;

    r->lo = lo, r->hi = hi, r->calls++;
    return true;
}

static SDL_JoystickID attach(const char *name, Uint16 vid, Uint16 pid, int gamepad, Rumble *rumble)
{
    SDL_VirtualJoystickDesc d;
    SDL_JoystickID id;

    SDL_INIT_INTERFACE(&d);
    d.vendor_id = vid;
    d.product_id = pid;
    d.name = name;
    if (gamepad) {
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.button_mask = 0x1fffff;   /* the standard buttons and the four paddles */
        d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    } else {
        /* a DirectInput arcade stick: ten buttons, a hat, two axes, no gamepad mapping */
        d.type = SDL_JOYSTICK_TYPE_ARCADE_STICK;
        d.naxes = 2;
        d.nbuttons = 10;
        d.nhats = 1;
    }
    if (rumble) {
        d.userdata = rumble;
        d.Rumble = on_rumble;
    }
    id = SDL_AttachVirtualJoystick(&d);
    CHECK(id != 0, "attach %s: %s", name, SDL_GetError());
    return id;
}

static SDL_Joystick *joy_of(SDL_JoystickID id)
{
    SDL_Joystick *j = SDL_GetJoystickFromID(id);

    CHECK(j != NULL, "no joystick %u", (unsigned)id);
    return j;
}

/* Only the test's own (virtual) controllers: a real pad plugged into the machine must not change the results.
   Removals pass. */
static int physical(const SDL_Event *e)
{
    SDL_JoystickID id;

    switch (e->type) {
    case SDL_EVENT_GAMEPAD_ADDED: id = e->gdevice.which; break;
    case SDL_EVENT_JOYSTICK_ADDED: id = e->jdevice.which; break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: id = e->gbutton.which; break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: id = e->gaxis.which; break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP: id = e->jbutton.which; break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: id = e->jhat.which; break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION: id = e->jaxis.which; break;
    default: return 0;
    }
    return !SDL_IsJoystickVirtual(id);
}

static void pump(PsiInput *in)
{
    SDL_Event e;

    SDL_UpdateJoysticks();
    while (SDL_PollEvent(&e)) {
        if (!physical(&e)) {
            psi_event(in, &e);
        }
    }
}

static void key(PsiInput *in, SDL_Scancode k, int down)
{
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = k;
    e.key.down = down != 0;
    psi_event(in, &e);
}

static void pads(PsiInput *in, PsiPad p[PSI_PLAYERS])
{
    pump(in);
    psi_pads(in, p);
}

/* One button of a virtual gamepad, held alone: the word it gives a player. */
static uint32_t press_one(PsiInput *in, SDL_Joystick *j, int button, int player)
{
    PsiPad p[PSI_PLAYERS];
    uint32_t got;

    SDL_SetJoystickVirtualButton(j, button, true);
    pads(in, p);
    got = p[player].buttons;
    SDL_SetJoystickVirtualButton(j, button, false);
    pads(in, p);
    return got;
}

static uint32_t press_any(PsiInput *in, SDL_Joystick *j, int button)
{
    return press_one(in, j, button, 0) | press_one(in, j, button, 1);
}

static PsiInput *make(const PsiLayout *lay, Mem *m, const char *name)
{
    PsiOptions o;

    memset(&o, 0, sizeof(o));
    o.layout = lay;
    o.name = name;
    o.only_virtual = 1;   /* a real pad on the test machine never takes a player */
    if (m) {
        o.store.ctx = m;
        o.store.get = mem_get;
        o.store.set = mem_set;
        o.store.save = mem_save;
    }
    return psi_new(&o);
}

/* Every PlayStation button on a pad by position (south X, east Circle...), in port A's bits. */
static void check_by_position(PsiInput *in, const char *what, SDL_Joystick *j, int player)
{
    static const struct {
        int button;
        uint32_t want;
        const char *name;
    } MAP[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH, A_X, "south: X" },
        { SDL_GAMEPAD_BUTTON_EAST, A_CIRCLE, "east: Circle" },
        { SDL_GAMEPAD_BUTTON_WEST, A_SQUARE, "west: Square" },
        { SDL_GAMEPAD_BUTTON_NORTH, A_TRIANGLE, "north: Triangle" },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, A_L1, "L1" },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, A_R1, "R1" },
        { SDL_GAMEPAD_BUTTON_BACK, A_SELECT, "Create/Share/View: Select" },
        { SDL_GAMEPAD_BUTTON_START, A_START, "Options/Menu: Start" },
        { SDL_GAMEPAD_BUTTON_DPAD_UP, A_UP, "d-pad up" },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, A_DOWN, "d-pad down" },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, A_LEFT, "d-pad left" },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, A_RIGHT, "d-pad right" },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, A_L3, "L3" },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, A_R3, "R3" },
        { SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, A_L1, "Deck L4: L1" },
        { SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, A_R1, "Deck R4: R1" },
        { SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, A_L2, "Deck L5: L2" },
        { SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, A_R2, "Deck R5: R2" },
    };
    PsiPad p[PSI_PLAYERS];
    size_t i;

    for (i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
        uint32_t got = press_one(in, j, MAP[i].button, player);

        CHECK(got == MAP[i].want, "%s, %s: got %04x, want %04x", what, MAP[i].name, got, MAP[i].want);
    }
    /* the triggers are L2 and R2 past a quarter */
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 20000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 20000);
    pads(in, p);
    CHECK(p[player].buttons == (A_L2 | A_R2), "%s, triggers: %04x", what, p[player].buttons);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);
    /* the left stick steers past half way, not inside it */
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, -30000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, 30000);
    pads(in, p);
    CHECK(p[player].buttons == (A_LEFT | A_DOWN), "%s, left stick down-left: %04x", what, p[player].buttons);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, -10000);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, 0);
    pads(in, p);
    CHECK(p[player].buttons == 0, "%s, stick inside half way: %04x", what, p[player].buttons);
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, 0);
    pads(in, p);
}

/* ------------------------------------------------------------------ port A */

static void test_port_a(void)
{
    Mem m;
    PsiInput *in, *in2;
    PsiPad p[PSI_PLAYERS];
    SDL_JoystickID ds5, ds4, xbox, stick, x2;
    SDL_Joystick *j5, *j4, *jx, *js;
    char text[256];

    memset(&m, 0, sizeof(m));
    in = make(&a_layout, &m, "porta");
    CHECK(in != NULL, "psi_new");
    if (!in) {
        return;
    }

    /* families from SDL's types and IDs */
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6, 0) == PSI_FAMILY_PLAYSTATION, "DualSense");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x09cc, 0) == PSI_FAMILY_PLAYSTATION, "DualShock 4");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b12, 0) == PSI_FAMILY_XBOX, "Xbox Series");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1205, 0) == PSI_FAMILY_STEAM, "Steam Deck");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_XBOX360, 0x28de, 0x11ff, 1) == PSI_FAMILY_STEAM, "Steam Input on a Deck");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_XBOX360, 0x28de, 0x11ff, 0) == PSI_FAMILY_XBOX, "Steam Input off a Deck");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009, 0) == PSI_FAMILY_NINTENDO, "Switch Pro");
    CHECK(psi_gamepad_family(SDL_GAMEPAD_TYPE_STANDARD, 0x0f0d, 0x0011, 0) == PSI_FAMILY_OTHER, "another pad");

    /* the keyboard: player 1's keys, with no pad connected */
    key(in, SDL_SCANCODE_X, 1);
    key(in, SDL_SCANCODE_LEFT, 1);
    pads(in, p);
    CHECK(p[0].buttons == (A_X | A_LEFT), "keys X + Left: %04x", p[0].buttons);
    CHECK(p[1].buttons == 0, "player 2 has no keys: %04x", p[1].buttons);
    CHECK(!p[0].analog && p[0].lx == 0x80 && p[0].ly == 0x80, "digital, sticks centred");
    key(in, SDL_SCANCODE_RIGHT, 1);
    key(in, SDL_SCANCODE_UP, 1);
    key(in, SDL_SCANCODE_DOWN, 1);
    pads(in, p);
    CHECK(p[0].buttons == (A_X | A_UP), "SOCD: left+right neutral, up+down up: %04x", p[0].buttons);
    psi_raw(in, p);
    CHECK(p[0].buttons == (A_X | A_LEFT | A_RIGHT | A_UP | A_DOWN), "raw: no SOCD: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_X, 0);
    key(in, SDL_SCANCODE_LEFT, 0);
    key(in, SDL_SCANCODE_RIGHT, 0);
    key(in, SDL_SCANCODE_UP, 0);
    key(in, SDL_SCANCODE_DOWN, 0);
    {
        static const struct {
            SDL_Scancode k;
            uint32_t want;
        } KEYS[] = {
            { SDL_SCANCODE_S, A_CIRCLE }, { SDL_SCANCODE_Z, A_SQUARE }, { SDL_SCANCODE_A, A_TRIANGLE },
            { SDL_SCANCODE_Q, A_L1 },     { SDL_SCANCODE_W, A_R1 },     { SDL_SCANCODE_E, A_L2 },
            { SDL_SCANCODE_R, A_R2 },     { SDL_SCANCODE_RETURN, A_START }, { SDL_SCANCODE_RSHIFT, A_SELECT },
        };
        size_t i;

        for (i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
            key(in, KEYS[i].k, 1);
            pads(in, p);
            CHECK(p[0].buttons == KEYS[i].want, "key %s: %04x, want %04x", SDL_GetScancodeName(KEYS[i].k), p[0].buttons,
                  KEYS[i].want);
            key(in, KEYS[i].k, 0);
        }
    }
    /* a window that loses focus lets go of every key */
    key(in, SDL_SCANCODE_X, 1);
    {
        SDL_Event e;

        memset(&e, 0, sizeof(e));
        e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        psi_event(in, &e);
    }
    pads(in, p);
    CHECK(p[0].buttons == 0, "focus lost: no key stays held: %04x", p[0].buttons);

    /* three pads and an arcade stick connect: players 1, 2, then 1 again */
    ds5 = attach("DualSense Wireless Controller (virtual)", 0x054c, 0x0ce6, 1, NULL);
    pump(in);
    ds4 = attach("DualShock 4 (virtual)", 0x054c, 0x09cc, 1, NULL);
    pump(in);
    xbox = attach("Xbox Series X Controller (virtual)", 0x045e, 0x0b12, 1, NULL);
    pump(in);
    stick = attach("Arcade stick (virtual)", 0x0f0d, 0x0011, 0, NULL);
    pump(in);
    psi_describe(in, stdout);
    CHECK(psi_device_count(in) == 4, "four devices: %d", psi_device_count(in));
    CHECK(SDL_GetGamepadTypeForID(ds5) == SDL_GAMEPAD_TYPE_PS5, "the DualSense is a PS5 pad to SDL");
    CHECK(SDL_GetGamepadTypeForID(xbox) == SDL_GAMEPAD_TYPE_XBOXONE, "the Xbox pad is an Xbox One pad to SDL");
    CHECK(!SDL_IsGamepad(stick), "the arcade stick has no gamepad mapping");
    j5 = joy_of(ds5), j4 = joy_of(ds4), jx = joy_of(xbox), js = joy_of(stick);
    psi_player_devices(in, 0, text, sizeof(text));
    CHECK(strstr(text, "DualSense") && strstr(text, "Xbox") && strstr(text, "keyboard"),
          "player 1 has the DualSense, the Xbox pad and the keyboard: %s", text);
    psi_player_devices(in, 1, text, sizeof(text));
    CHECK(strstr(text, "DualShock 4") != NULL, "player 2 has the DualShock 4: %s", text);

    /* every button by position, on each pad, with no setup */
    check_by_position(in, "DualSense (054c:0ce6)", j5, 0);
    check_by_position(in, "DualShock 4 (054c:09cc)", j4, 1);
    check_by_position(in, "Xbox Series (045e:0b12)", jx, 0);

    /* the keyboard and two pads of player 1's at once */
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, true);
    key(in, SDL_SCANCODE_Z, 1);
    pads(in, p);
    CHECK(p[0].buttons == (A_X | A_TRIANGLE | A_SQUARE), "keyboard and two pads together: %04x", p[0].buttons);
    CHECK(p[1].buttons == 0, "player 2 untouched: %04x", p[1].buttons);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, false);
    key(in, SDL_SCANCODE_Z, 0);
    pads(in, p);

    /* the arcade stick: DirectInput order, the hat */
    SDL_SetJoystickVirtualButton(js, 0, true);
    SDL_SetJoystickVirtualButton(js, 9, true);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_RIGHTUP);
    pads(in, p);
    CHECK(p[0].buttons == (A_SQUARE | A_START | A_RIGHT | A_UP), "arcade stick: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(js, 0, false);
    SDL_SetJoystickVirtualButton(js, 9, false);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_CENTERED);
    pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == 0, "all released: %04x %04x", p[0].buttons, p[1].buttons);

    /* the on-screen controller is player 1's too */
    psi_set_touch(in, 0, A_CIRCLE | A_LEFT);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_WEST, true);
    pads(in, p);
    CHECK(p[0].buttons == (A_CIRCLE | A_LEFT | A_SQUARE), "touch and the DualSense: %04x", p[0].buttons);
    CHECK(p[1].buttons == 0, "touch is not player 2's");
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_WEST, false);
    psi_set_touch(in, 0, 0);
    pads(in, p);

    /* hot-plug: player 2's DualShock 4 leaves; the next pad to connect takes its place */
    SDL_DetachVirtualJoystick(ds4);
    pump(in);
    CHECK(psi_device_count(in) == 3, "three devices after the DualShock 4 left: %d", psi_device_count(in));
    psi_player_devices(in, 1, text, sizeof(text));
    CHECK(!strcmp(text, "none"), "player 2 has none: %s", text);
    ds4 = attach("DualShock 4 (virtual, again)", 0x054c, 0x09cc, 1, NULL);
    pump(in);
    j4 = joy_of(ds4);
    CHECK(press_one(in, j4, SDL_GAMEPAD_BUTTON_EAST, 1) == A_CIRCLE, "the DualShock 4 is player 2 again");

    /* swapping the players' devices */
    psi_swap_players(in);
    CHECK(press_one(in, j4, SDL_GAMEPAD_BUTTON_NORTH, 0) == A_TRIANGLE, "swapped: the DualShock 4 is player 1");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_NORTH, 1) == A_TRIANGLE, "swapped: the DualSense is player 2");
    psi_swap_players(in);

    /* analog mode: the sticks' bytes; a key steers the centred left stick */
    psi_set_analog(in, 0, 1);
    CHECK(!strcmp(mget(&m, "p1.analog"), "on"), "p1.analog saved: %s", mget(&m, "p1.analog"));
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_RIGHTY, -32768);
    pads(in, p);
    CHECK(p[0].analog && p[0].lx == 0xFF && p[0].ly == 0x80 && p[0].ry == 0x00 && p[0].rx == 0x80,
          "analog: sticks: lx %02x ly %02x rx %02x ry %02x", p[0].lx, p[0].ly, p[0].rx, p[0].ry);
    CHECK(!p[1].analog && p[1].lx == 0x80, "player 2 stays digital");
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_LEFTX, 0);
    SDL_SetJoystickVirtualAxis(j5, SDL_GAMEPAD_AXIS_RIGHTY, 0);
    key(in, SDL_SCANCODE_LEFT, 1);
    pads(in, p);
    CHECK(p[0].lx == 0x00 && p[0].ly == 0x80 && (p[0].buttons & A_LEFT), "a key in analog mode steers: %02x %02x", p[0].lx,
          p[0].ly);
    key(in, SDL_SCANCODE_LEFT, 0);
    psi_toggle_analog(in, 0);
    CHECK(!psi_analog(in, 0) && !strcmp(mget(&m, "p1.analog"), "off"), "toggled off and saved");
    pads(in, p);

    /* remapping one button: player 1's X onto the east button; the keyboard binding stays */
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_NORTH, true);   /* held from before */
    pump(in);
    psi_remap_begin(in, 0, PSI_PS_X);
    CHECK(psi_remap_slot(in) == PSI_PS_X, "capturing X");
    CHECK(psi_remap_prompt(in) == NULL, "a single capture has no prompt run");
    psi_pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == 0, "nothing reaches the game while capturing: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(j5, SDL_GAMEPAD_BUTTON_NORTH, false);
    pump(in);
    CHECK(psi_remap_slot(in) == PSI_PS_X, "a release is not a capture");
    SDL_Delay(350);   /* past the gap that keeps the starting press out */
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    pump(in);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, false);
    pads(in, p);
    CHECK(psi_remap_slot(in) < 0 && psi_remap_take_result(in) == 1 && psi_remap_take_result(in) == 0, "X captured, once");
    CHECK(m.saves == 1, "the store was asked to save: %d", m.saves);
    CHECK(press_one(in, jx, SDL_GAMEPAD_BUTTON_EAST, 0) == (A_X | A_CIRCLE), "east is X and Circle now");
    CHECK(press_one(in, jx, SDL_GAMEPAD_BUTTON_SOUTH, 0) == 0, "south is nothing now");
    psi_binding_text(in, 0, PSI_PS_X, text, sizeof(text));
    CHECK(strstr(text, "X key") && strstr(text, "east"), "X's bindings as a person reads them: %s", text);
    /* the other kinds' bindings stay, the new one goes last */
    CHECK(!strcmp(mget(&m, "p1.x"), "key:X, joy:b1, pad:east"), "saved as text: %s", mget(&m, "p1.x"));
    /* Escape cancels and keeps it */
    psi_remap_begin(in, 0, PSI_PS_CIRCLE);
    key(in, SDL_SCANCODE_ESCAPE, 1);
    key(in, SDL_SCANCODE_ESCAPE, 0);
    CHECK(psi_remap_slot(in) < 0 && psi_remap_take_result(in) == 2, "Escape cancels");
    CHECK(m.saves == 1, "a cancel saves nothing");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == (A_X | A_CIRCLE), "Circle kept east");
    psi_remap_begin(in, 0, PSI_PS_L1);
    psi_remap_cancel(in);
    CHECK(psi_remap_slot(in) < 0 && psi_remap_take_result(in) == 2, "cancelled by the port");

    /* a key whose name holds a comma or a '#' is saved by its number, and read back */
    psi_remap_begin(in, 0, PSI_PS_START);
    SDL_Delay(350);
    key(in, SDL_SCANCODE_COMMA, 1);
    key(in, SDL_SCANCODE_COMMA, 0);
    CHECK(psi_remap_take_result(in) == 1, "comma captured");
    CHECK(!strcmp(mget(&m, "p1.start"), "key:Return, pad:start, joy:b9, key:sc54") ||
              !strcmp(mget(&m, "p1.start"), "pad:start, joy:b9, key:sc54"),
          "a comma is not written as a comma: %s", mget(&m, "p1.start"));
    psi_remap_begin(in, 0, PSI_PS_SELECT);
    SDL_Delay(350);
    key(in, SDL_SCANCODE_NONUSHASH, 1);
    key(in, SDL_SCANCODE_NONUSHASH, 0);
    CHECK(psi_remap_take_result(in) == 1 && strstr(mget(&m, "p1.select"), "key:sc50") != NULL, "Non-US # by number: %s",
          mget(&m, "p1.select"));

    /* a new run reads what was saved; reset brings the defaults back */
    psi_free(in);
    in2 = make(&a_layout, &m, "porta");
    in = in2;
    /* the old layer closed its handles: these are the new layer's */
    j5 = joy_of(ds5), j4 = joy_of(ds4), jx = joy_of(xbox), js = joy_of(stick);
    CHECK(psi_device_count(in) == 4, "the devices already connected are opened: %d", psi_device_count(in));
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == (A_X | A_CIRCLE), "the remap survives a restart");
    CHECK(press_any(in, jx, SDL_GAMEPAD_BUTTON_EAST) != 0, "the Xbox pad is someone's");
    key(in, SDL_SCANCODE_COMMA, 1);
    pads(in, p);
    CHECK(p[0].buttons == A_START, "the comma key is Start after a restart: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_COMMA, 0);
    key(in, SDL_SCANCODE_NONUSHASH, 1);
    pads(in, p);
    CHECK(p[0].buttons == A_SELECT, "and Non-US # is Select: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_NONUSHASH, 0);
    m.saves = 0;
    psi_reset_player(in, 0);
    CHECK(m.saves == 1, "reset saves");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_EAST, 0) == A_CIRCLE, "reset: east is Circle again");
    CHECK(press_one(in, j5, SDL_GAMEPAD_BUTTON_SOUTH, 0) == A_X, "reset: south is X again");
    key(in, SDL_SCANCODE_COMMA, 1);
    pads(in, p);
    CHECK(p[0].buttons == 0, "reset: the comma key does nothing: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_COMMA, 0);

    /* pinning: a pad's GUID in the store puts that pad on player 2 whatever connects first */
    {
        char guid[64];

        psi_free(in);
        SDL_DetachVirtualJoystick(ds5);
        SDL_DetachVirtualJoystick(ds4);
        SDL_DetachVirtualJoystick(xbox);
        SDL_DetachVirtualJoystick(stick);
        SDL_UpdateJoysticks();
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
        x2 = attach("Xbox Series X Controller (virtual)", 0x045e, 0x0b12, 1, NULL);
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(x2), guid, sizeof(guid));
        mem_set(&m, "p2.device", guid);
        in = make(&a_layout, &m, "porta");
        CHECK(press_one(in, joy_of(x2), SDL_GAMEPAD_BUTTON_SOUTH, 1) == A_X, "the pinned pad is player 2's");
        /* pin it back to player 1 while it is connected */
        psi_pin_device(in, 0, guid);
        CHECK(!strcmp(mget(&m, "p1.device"), guid), "pinned in the store");
        CHECK(press_one(in, joy_of(x2), SDL_GAMEPAD_BUTTON_SOUTH, 0) == A_X, "pinned to player 1: it moved at once");
        SDL_DetachVirtualJoystick(x2);
        pump(in);
        CHECK(psi_device_count(in) == 0, "unplugged");
        psi_describe(in, stdout);
    }
    psi_free(in);

    /* "socd = off" passes opposite directions on */
    memset(&m, 0, sizeof(m));
    mem_set(&m, "socd", "off");
    in = make(&a_layout, &m, "porta");
    key(in, SDL_SCANCODE_LEFT, 1);
    key(in, SDL_SCANCODE_RIGHT, 1);
    pads(in, p);
    CHECK(p[0].buttons == (A_LEFT | A_RIGHT), "socd = off: %04x", p[0].buttons);
    psi_free(in);

    /* a bad binding in the store is reported and ignored; the rest of the line stays */
    memset(&m, 0, sizeof(m));
    mem_set(&m, "p1.x", "key:Not A Key, pad:south, bogus:1, joy:b9999");
    in = make(&a_layout, &m, "porta");
    {
        SDL_JoystickID one = attach("Xbox Series X Controller (virtual)", 0x045e, 0x0b12, 1, NULL);

        pump(in);
        CHECK(press_one(in, joy_of(one), SDL_GAMEPAD_BUTTON_SOUTH, 0) == A_X, "the good part of a line with bad parts works");
        SDL_DetachVirtualJoystick(one);
        pump(in);
    }
    psi_free(in);

    /* no store at all: the defaults, and a remap that cannot be saved still works */
    in = make(&a_layout, NULL, "porta");
    CHECK(in != NULL, "a layer with no store");
    psi_remap_begin(in, 0, PSI_PS_X);
    SDL_Delay(350);
    key(in, SDL_SCANCODE_J, 1);
    CHECK(psi_remap_take_result(in) == 1, "captured with no store");
    key(in, SDL_SCANCODE_J, 0);
    key(in, SDL_SCANCODE_J, 1);
    pads(in, p);
    CHECK(p[0].buttons == A_X, "J is X");
    key(in, SDL_SCANCODE_J, 0);
    psi_set_analog(in, 0, 1);
    psi_free(in);

    /* a layout the layer cannot use */
    {
        PsiOptions o;
        PsiLayout bad = a_layout;

        memset(&o, 0, sizeof(o));
        CHECK(psi_new(&o) == NULL, "no layout");
        o.layout = &bad;
        bad.nslots = 0;
        CHECK(psi_new(&o) == NULL, "no slots");
        bad.nslots = PSI_MAX_SLOTS + 1;
        CHECK(psi_new(&o) == NULL, "too many slots");
        bad = a_layout;
        {
            int order[2] = { 0, 99 };

            bad.remap_order = order;
            bad.nremap = 2;
            CHECK(psi_new(&o) == NULL, "a remap order with a slot that is not there");
        }
    }
}

/* ------------------------------------------------------------------ port B */

static const char *letters(PsiInput *in, int player, char *out9)
{
    static const int SLOTS[4] = { PSI_PS_CIRCLE, PSI_PS_X, PSI_PS_SQUARE, PSI_PS_TRIANGLE };
    int i;

    psi_prompt_letters(in, player, SLOTS, 4, out9);
    for (i = 0; i < 4; i++) {
        if (!out9[i]) {
            out9[i] = '.';
        }
    }
    out9[4] = 0;
    return out9;
}

static void test_port_b(void)
{
    Mem m;
    PsiInput *in;
    PsiPad p[PSI_PLAYERS];
    SDL_JoystickID xbox, ds, deck, stick, sw;
    SDL_Joystick *jx, *jd, *jk, *js;
    Rumble rx = { 0, 0, 0 }, rd = { 0, 0, 0 }, rk = { 0, 0, 0 };
    char l[16];
    int p1;

    memset(&m, 0, sizeof(m));
    in = make(&b_layout, &m, "portb");
    CHECK(in != NULL, "psi_new");
    if (!in) {
        return;
    }
    for (p1 = 0; p1 < 2; p1++) {
        set_macros(in, p1, &DEFAULT_KEYS);
    }
    CHECK(psi_family(in) == PSI_FAMILY_KEYBOARD, "family before any device: %s", psi_family_name(psi_family(in)));
    CHECK(psi_last_player(in) == 0, "last player");
    key(in, SDL_SCANCODE_Z, 1);
    key(in, SDL_SCANCODE_LEFT, 1);
    pads(in, p);
    CHECK(p[0].buttons == (B_CROSS | B_LEFT), "keys: %04x", p[0].buttons);
    CHECK(p[1].buttons == 0, "player 2 has no keys: %04x", p[1].buttons);
    /* prompt letters, circle X square triangle: the keys X Z A S; player 2 has none */
    CHECK(!strcmp(letters(in, 0, l), "XZAS"), "keyboard letters: %s", l);
    CHECK(!strcmp(letters(in, 1, l), "...."), "player 2's letters: %s", l);
    key(in, SDL_SCANCODE_Z, 0);
    key(in, SDL_SCANCODE_LEFT, 0);
    psi_free(in);
    /* a config written under an earlier name for X is still read */
    memset(&m, 0, sizeof(m));
    mem_set(&m, "p1.cross", "key:K");
    in = make(&b_layout, &m, "portb");
    key(in, SDL_SCANCODE_K, 1);
    pads(in, p);
    CHECK(p[0].buttons == B_CROSS, "p1.cross still read: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_K, 0);
    psi_free(in);

    memset(&m, 0, sizeof(m));
    in = make(&b_layout, &m, "portb");
    set_macros(in, 0, &DEFAULT_KEYS);
    set_macros(in, 1, &DEFAULT_KEYS);

    /* three gamepads and an arcade stick: players 1, 2, then 1 */
    xbox = attach("Xbox Series (virtual)", 0x045e, 0x0b12, 1, &rx);
    pump(in);
    ds = attach("DualSense (virtual)", 0x054c, 0x0ce6, 1, &rd);
    pump(in);
    deck = attach("Steam Deck (virtual)", 0x28de, 0x1205, 1, &rk);
    pump(in);
    stick = attach("Arcade stick (virtual)", 0x0f0d, 0x0011, 0, NULL);
    pump(in);
    jx = joy_of(xbox), jd = joy_of(ds), jk = joy_of(deck), js = joy_of(stick);
    CHECK(psi_family(in) == PSI_FAMILY_XBOX, "the first device claims the family: %s", psi_family_name(psi_family(in)));

    /* Xbox pad, player 1: positions, the stick past half way, the trigger */
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    SDL_SetJoystickVirtualAxis(jx, SDL_GAMEPAD_AXIS_LEFTX, -30000);
    SDL_SetJoystickVirtualAxis(jx, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 20000);
    pads(in, p);
    CHECK(p[0].buttons == (B_CROSS | B_R1 | B_LEFT | B_L2), "Xbox: %04x", p[0].buttons);
    CHECK(psi_family(in) == PSI_FAMILY_XBOX, "family after the Xbox pad: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 0, l), "BAXY"), "Xbox letters: %s", l);
    CHECK(psi_last_player(in) == 0, "last player after the Xbox pad");
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
    SDL_SetJoystickVirtualAxis(jx, SDL_GAMEPAD_AXIS_LEFTX, 0);
    SDL_SetJoystickVirtualAxis(jx, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_JOYSTICK_AXIS_MIN);

    /* macros: L3 is P AND K, R3 S AND HS: both parts on the same field, nothing else */
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
    pads(in, p);
    CHECK(p[0].buttons == (B_SQUARE | B_CROSS), "P AND K on L3: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
    pads(in, p);
    CHECK(p[0].buttons == (B_SQUARE | B_CROSS | B_TRIANGLE | B_CIRCLE), "both macros: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
    pads(in, p);
    CHECK(p[0].buttons == 0, "macros released together: %04x", p[0].buttons);
    {
        /* the game's own button config with P on L2: P AND K follows it, for player 1 only */
        GameKeys k = DEFAULT_KEYS;

        k.punch = B_L2;
        set_macros(in, 0, &k);
        SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_LEFT_STICK, true);
        pads(in, p);
        CHECK(p[0].buttons == (B_L2 | B_CROSS), "P AND K with the game's P on L2: %04x", p[0].buttons);
        SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_LEFT_STICK, false);
        set_macros(in, 0, &DEFAULT_KEYS);
    }
    /* each keyboard macro on its own: D, F, C, V */
    {
        static const struct {
            SDL_Scancode k;
            uint32_t want;
        } KM[] = {
            { SDL_SCANCODE_D, B_SQUARE | B_CROSS },
            { SDL_SCANCODE_F, B_TRIANGLE | B_CIRCLE },
            { SDL_SCANCODE_C, B_SQUARE | B_CROSS | B_TRIANGLE },
            { SDL_SCANCODE_V, B_SQUARE | B_CROSS | B_TRIANGLE | B_CIRCLE },
        };
        size_t i;

        for (i = 0; i < sizeof(KM) / sizeof(KM[0]); i++) {
            key(in, KM[i].k, 1);
            pads(in, p);
            CHECK(p[0].buttons == KM[i].want, "keyboard macro %d: %04x, not %04x", (int)i, p[0].buttons, KM[i].want);
            key(in, KM[i].k, 0);
            pads(in, p);
        }
    }
    /* with a direction, SOCD still applies */
    key(in, SDL_SCANCODE_V, 1);
    key(in, SDL_SCANCODE_LEFT, 1);
    key(in, SDL_SCANCODE_RIGHT, 1);
    pads(in, p);
    CHECK(p[0].buttons == (B_SQUARE | B_CROSS | B_TRIANGLE | B_CIRCLE), "P K S HS on V, left+right: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_V, 0);
    key(in, SDL_SCANCODE_LEFT, 0);
    key(in, SDL_SCANCODE_RIGHT, 0);
    /* the Xbox pad again, so it is player 1's last device */
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pump(in);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pads(in, p);

    /* the swap: while the port says so, X and Circle exchange; a press keeps its meaning until released */
    psi_set_swap(in, 0, 1);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pads(in, p);
    CHECK(p[0].buttons == B_CIRCLE, "swap on, south: %04x", p[0].buttons);
    psi_set_swap(in, 0, 0);
    pads(in, p);
    CHECK(p[0].buttons == B_CIRCLE, "swap off while held: south is still Circle: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    pads(in, p);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, true);
    pads(in, p);
    CHECK(p[0].buttons == B_CROSS, "swap off, a new press of south: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_SOUTH, false);
    psi_set_swap(in, 0, 1);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    pads(in, p);
    CHECK(p[0].buttons == B_CROSS, "swap on, east: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, false);
    psi_raw(in, p);
    psi_set_swap(in, 1, 1);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, true);
    pads(in, p);
    CHECK(p[0].buttons == B_TRIANGLE, "other buttons pass through the swap: %04x", p[0].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, false);
    psi_set_swap(in, 0, 0);
    psi_set_swap(in, 1, 0);
    pads(in, p);

    /* DualSense, player 2 */
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_EAST, true);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_START, true);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
    pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == (B_CIRCLE | B_START | B_DOWN), "DualSense: %04x %04x", p[0].buttons,
          p[1].buttons);
    CHECK(psi_family(in) == PSI_FAMILY_PLAYSTATION, "family after the DualSense: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 1, l), "...."), "DualSense letters (the shapes): %s", l);
    CHECK(!strcmp(letters(in, 0, l), "BAXY"), "player 1 still the Xbox pad: %s", l);
    CHECK(psi_last_player(in) == 1, "last player after the DualSense");
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_EAST, false);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_START, false);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);

    /* the Steam Deck's controls, player 1 (a third device): the back buttons are macros */
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_WEST, true);
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, true);   /* R4 */
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, true);    /* L5 */
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_BACK, true);
    pads(in, p);
    /* west (square), R4 (S AND HS), L5 (P K S), back (select) */
    CHECK(p[0].buttons == (B_SQUARE | B_TRIANGLE | B_CIRCLE | B_CROSS | B_SELECT), "Deck: %04x", p[0].buttons);
    CHECK(psi_family(in) == PSI_FAMILY_STEAM, "family after the Deck: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 0, l), "BAXY"), "Deck letters: %s", l);
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_WEST, false);
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, false);
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, false);
    SDL_SetJoystickVirtualButton(jk, SDL_GAMEPAD_BUTTON_BACK, false);

    /* the arcade stick, a plain joystick: the fourth device, so player 1 */
    SDL_SetJoystickVirtualButton(js, 0, true);
    SDL_SetJoystickVirtualButton(js, 9, true);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_RIGHTUP);
    pads(in, p);
    CHECK(p[0].buttons == (B_SQUARE | B_START | B_RIGHT | B_UP), "arcade stick: %04x", p[0].buttons);
    CHECK(psi_family(in) == PSI_FAMILY_OTHER, "family after the stick: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 0, l), "...."), "arcade stick letters (none): %s", l);
    SDL_SetJoystickVirtualButton(js, 0, false);
    SDL_SetJoystickVirtualButton(js, 9, false);
    SDL_SetJoystickVirtualHat(js, 0, SDL_HAT_CENTERED);
    pads(in, p);
    CHECK(p[0].buttons == 0 && p[1].buttons == 0, "all released: %04x %04x", p[0].buttons, p[1].buttons);

    /* a Switch Pro pad (player 1): Nintendo's letters on the same positions, B at the bottom */
    sw = attach("Switch Pro (virtual)", 0x057e, 0x2009, 1, NULL);
    pump(in);
    SDL_SetJoystickVirtualButton(joy_of(sw), SDL_GAMEPAD_BUTTON_EAST, true);
    pump(in);
    pads(in, p);
    CHECK(psi_family(in) == PSI_FAMILY_NINTENDO, "family after the Switch Pro: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 0, l), "ABYX"), "Switch Pro letters: %s", l);
    SDL_SetJoystickVirtualButton(joy_of(sw), SDL_GAMEPAD_BUTTON_EAST, false);
    pump(in);
    /* a key of player 1's: the keyboard again */
    key(in, SDL_SCANCODE_X, 1);
    key(in, SDL_SCANCODE_X, 0);
    CHECK(psi_family(in) == PSI_FAMILY_KEYBOARD, "family after a key: %s", psi_family_name(psi_family(in)));
    CHECK(!strcmp(letters(in, 0, l), "XZAS"), "back on the keyboard: %s", l);
    SDL_DetachVirtualJoystick(sw);
    pump(in);

    /* the players' devices swap */
    psi_swap_players(in);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_NORTH, true);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, true);
    pads(in, p);
    CHECK(p[0].buttons == B_TRIANGLE && p[1].buttons == B_TRIANGLE, "swapped: %04x %04x", p[0].buttons, p[1].buttons);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_NORTH, false);
    pads(in, p);
    CHECK(p[0].buttons == B_TRIANGLE && p[1].buttons == 0, "the DualSense is player 1 now: %04x %04x", p[0].buttons,
          p[1].buttons);
    SDL_SetJoystickVirtualButton(jd, SDL_GAMEPAD_BUTTON_NORTH, false);
    psi_swap_players(in);

    /* forced families */
    psi_force_family(in, PSI_FAMILY_XBOX);
    CHECK(psi_family(in) == PSI_FAMILY_XBOX, "forced Xbox");
    CHECK(!strcmp(letters(in, 0, l), "BAXY") && !strcmp(letters(in, 1, l), "BAXY"), "forced Xbox letters: %s", l);
    psi_force_family(in, PSI_FAMILY_NINTENDO);
    CHECK(!strcmp(letters(in, 0, l), "ABYX"), "forced Nintendo letters: %s", l);
    psi_force_family(in, PSI_FAMILY_PLAYSTATION);
    CHECK(!strcmp(letters(in, 0, l), "...."), "forced PlayStation letters: %s", l);
    psi_force_family(in, -1);
    CHECK(psi_family(in) == PSI_FAMILY_XBOX, "auto again: the last device used: %s", psi_family_name(psi_family(in)));

    /* rumble: the port's motors on player 1's gamepads (the Xbox pad and the Deck), not player 2's */
    psi_rumble(in, 0, 0xFFFF, 0xC0C0, 100);
    pump(in);
    CHECK(rx.calls == 1 && rx.lo == 0xC0C0 && rx.hi == 0xFFFF, "rumble on the Xbox pad: %d %04x %04x", rx.calls, rx.lo, rx.hi);
    CHECK(rk.calls == 1, "and on the Deck: %d", rk.calls);
    CHECK(rd.calls == 0, "not on player 2's pad: %d", rd.calls);
    psi_rumble(in, 0, 0, 0, 0);
    CHECK(rx.calls == 2 && rx.lo == 0 && rx.hi == 0, "rumble stopped");

    /* remapping a run for player 1: Up to J from the keyboard, Down to the Xbox pad's east button, then Escape for the
       rest */
    m.saves = 0;
    psi_remap_start(in, 0);
    CHECK(psi_remap_prompt(in) && strstr(psi_remap_prompt(in), "Up"), "prompt: %s", psi_remap_prompt(in));
    CHECK(psi_remap_slot(in) == PSI_PS_UP, "asking for Up");
    SDL_Delay(320);
    key(in, SDL_SCANCODE_J, 1);
    key(in, SDL_SCANCODE_J, 0);
    CHECK(psi_remap_prompt(in) && strstr(psi_remap_prompt(in), "Down"), "prompt: %s", psi_remap_prompt(in));
    SDL_Delay(320);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    pump(in);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, false);
    pump(in);
    {
        int macro_seen = 0, x_seen = 0;

        while (psi_remap_prompt(in)) {
            x_seen |= strstr(psi_remap_prompt(in), "press X (") != NULL;
            CHECK(!strstr(psi_remap_prompt(in), "Cross"), "prompt says Cross: %s", psi_remap_prompt(in));
            /* the extras are asked for after the buttons, with what they are bound to now */
            macro_seen |= strstr(psi_remap_prompt(in), "P AND K (now key:D, pad:leftstick, pad:l4") != NULL;
            key(in, SDL_SCANCODE_ESCAPE, 1);
            key(in, SDL_SCANCODE_ESCAPE, 0);
        }
        CHECK(macro_seen, "the run never asked for P AND K with its bindings");
        CHECK(x_seen, "the run never asked for X");
        CHECK(psi_remap_take_result(in) == 1, "the run finished");
        CHECK(m.saves == 1, "saved once, at the end of the run: %d", m.saves);
        CHECK(mem_get(&m, "p1.x") && !mem_get(&m, "p1.cross"), "a run writes every slot, under the current key");
        CHECK(mem_get(&m, "p1.p_k_s_hs") != NULL, "the extras too");
    }
    key(in, SDL_SCANCODE_J, 1);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, true);
    pads(in, p);
    /* J is Up, east is Down (and still Circle); up+down is up */
    CHECK(p[0].buttons == (B_UP | B_CIRCLE), "remapped: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_J, 0);
    SDL_SetJoystickVirtualButton(jx, SDL_GAMEPAD_BUTTON_EAST, false);
    key(in, SDL_SCANCODE_UP, 1);
    pads(in, p);
    CHECK(p[0].buttons == 0, "the Up key no longer moves: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_UP, 0);
    CHECK(strstr(mget(&m, "p1.up"), "key:J") && strstr(mget(&m, "p1.up"), "pad:dpup"), "p1.up = %s", mget(&m, "p1.up"));
    CHECK(!strcmp(mget(&m, "p1.down"), "key:Down, joy:h0.down, joy:a1+, pad:east"), "p1.down = %s", mget(&m, "p1.down"));
    /* a run can be cancelled by the port */
    psi_remap_start(in, 1);
    CHECK(psi_remap_prompt(in) != NULL, "a run for player 2");
    psi_remap_cancel(in);
    CHECK(psi_remap_prompt(in) == NULL && psi_remap_take_result(in) == 2, "cancelled");

    /* the saved settings, read back, with "prompts" forcing the family */
    psi_free(in);
    mem_set(&m, "prompts", "xbox");
    mem_set(&m, "rumble", "off");
    in = make(&b_layout, &m, "portb");
    set_macros(in, 0, &DEFAULT_KEYS);
    key(in, SDL_SCANCODE_J, 1);
    pads(in, p);
    CHECK(p[0].buttons == B_UP, "bindings from the store: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_J, 0);
    CHECK(psi_family(in) == PSI_FAMILY_XBOX, "prompts = xbox forces the family");
    CHECK(!strcmp(letters(in, 0, l), "BAXY") && !strcmp(letters(in, 1, l), "BAXY"), "forced Xbox letters from the store: %s", l);
    /* rumble = off */
    rx.calls = 0;
    psi_rumble(in, 0, 0xFFFF, 0xFFFF, 100);
    pump(in);
    CHECK(rx.calls == 0, "rumble = off: nothing sent");
    /* a macro that the port never gave bits presses nothing */
    key(in, SDL_SCANCODE_D, 1);
    pads(in, p);
    CHECK(p[0].buttons == (B_SQUARE | B_CROSS), "set_macros again: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_D, 0);
    psi_free(in);
    in = make(&b_layout, &m, "portb");
    key(in, SDL_SCANCODE_D, 1);
    pads(in, p);
    CHECK(p[0].buttons == 0, "an extra whose bits the port has not set presses nothing: %04x", p[0].buttons);
    key(in, SDL_SCANCODE_D, 0);
    psi_free(in);
    SDL_DetachVirtualJoystick(xbox);
    SDL_DetachVirtualJoystick(ds);
    SDL_DetachVirtualJoystick(deck);
    SDL_DetachVirtualJoystick(stick);
    SDL_UpdateJoysticks();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}

/* a log callback gets the lines instead of stderr */
static char logged[2048];

static void collect(void *user, const char *line)
{
    (void)user;
    strncat(logged, line, sizeof(logged) - strlen(logged) - 2);
    strcat(logged, "\n");
}

static void test_log(void)
{
    Mem m;
    PsiOptions o;
    PsiInput *in;

    memset(&m, 0, sizeof(m));
    mem_set(&m, "p1.x", "bogus:1");
    mem_set(&m, "prompts", "toaster");
    memset(&o, 0, sizeof(o));
    o.layout = &a_layout;
    o.store.ctx = &m;
    o.store.get = mem_get;
    o.log = collect;
    in = psi_new(&o);
    CHECK(in != NULL, "psi_new");
    CHECK(strstr(logged, "p1.x: \"bogus:1\" is not a binding") != NULL, "a bad binding is logged: %s", logged);
    CHECK(strstr(logged, "prompts = toaster") != NULL, "a bad family is logged: %s", logged);
    psi_free(in);
}

int main(int argc, char **argv)
{
    static const char *const OFF[] = {
        "SDL_JOYSTICK_HIDAPI", "SDL_JOYSTICK_RAWINPUT", "SDL_JOYSTICK_WGI", "SDL_XINPUT_ENABLED",
        "SDL_JOYSTICK_DIRECTINPUT", "SDL_JOYSTICK_GAMEINPUT", "SDL_JOYSTICK_MFI", "SDL_JOYSTICK_IOKIT",
    };
    size_t h;

    (void)argc, (void)argv;
    /* only the virtual controllers: a real pad plugged into the test machine must not take a player */
    for (h = 0; h < sizeof(OFF) / sizeof(OFF[0]); h++) {
        SDL_SetHint(OFF[h], "0");
    }
    if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        fprintf(stderr, "psi-test: SDL: %s\n", SDL_GetError());
        return 1;
    }
    printf("psi-test: SDL %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    build_ports();
    test_port_a();
    test_port_b();
    test_log();
    SDL_Quit();
    printf("psi-test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
