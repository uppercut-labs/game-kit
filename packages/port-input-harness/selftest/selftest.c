/* pih-selftest: a made-up port, so the harness is tested without any game, and an example of an adapter.
 *
 * The "port" is a small input layer: SDL gamepads and joysticks to two players' state words, the first
 * controller player 1, the second player 2, a third player 1 again. selftest.map says what each control gives.
 * A real port's adapter does the same three things: open its input layer, pump it, read a player's state. */
#include <stdlib.h>
#include <string.h>

#include "pih/pihrun.h"

#define MAXDEV 8

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gp;
    SDL_Joystick *joy;
    int player;
} Dev;

typedef struct {
    Dev dev[MAXDEV];
    int n;
} Layer;

static void *st_open(void *user)
{
    (void)user;
    return calloc(1, sizeof(Layer));
}

static void st_close(void *user, void *in)
{
    Layer *l = in;
    int i;

    (void)user;
    for (i = 0; i < l->n; i++) {
        if (l->dev[i].gp) {
            SDL_CloseGamepad(l->dev[i].gp);
        } else {
            SDL_CloseJoystick(l->dev[i].joy);
        }
    }
    free(l);
}

static void add(Layer *l, SDL_JoystickID id, int gamepad)
{
    int i, used[2] = { 0, 0 };
    Dev *d;

    for (i = 0; i < l->n; i++) {
        if (l->dev[i].id == id) {
            return;
        }
        used[l->dev[i].player] = 1;
    }
    if (l->n == MAXDEV) {
        return;
    }
    d = &l->dev[l->n];
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->gp = gamepad ? SDL_OpenGamepad(id) : NULL;
    d->joy = gamepad ? NULL : SDL_OpenJoystick(id);
    if (!d->gp && !d->joy) {
        return;   /* it went before the port looked */
    }
    d->player = !used[0] ? 0 : !used[1] ? 1 : 0;
    l->n++;
}

static void st_pump(void *user, void *in)
{
    Layer *l = in;
    SDL_Event e;
    int i;

    (void)user;
    SDL_UpdateJoysticks();
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            add(l, e.gdevice.which, 1);
        } else if (e.type == SDL_EVENT_JOYSTICK_ADDED && !SDL_IsGamepad(e.jdevice.which)) {
            add(l, e.jdevice.which, 0);
        } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED || e.type == SDL_EVENT_JOYSTICK_REMOVED) {
            SDL_JoystickID id = e.type == SDL_EVENT_GAMEPAD_REMOVED ? e.gdevice.which : e.jdevice.which;

            for (i = 0; i < l->n; i++) {
                if (l->dev[i].id == id) {
                    if (l->dev[i].gp) {
                        SDL_CloseGamepad(l->dev[i].gp);
                    } else {
                        SDL_CloseJoystick(l->dev[i].joy);
                    }
                    l->dev[i] = l->dev[--l->n];
                    break;
                }
            }
        }
    }
}

static uint32_t st_read(void *user, void *in, int player)
{
    Layer *l = in;
    uint32_t v = 0;
    int i;

    (void)user;
    for (i = 0; i < l->n; i++) {
        const Dev *d = &l->dev[i];
        int b;

        if (d->player != player) {
            continue;
        }
        if (d->gp) {
            static const struct {
                SDL_GamepadButton b;
                uint32_t bit;
            } BTN[] = { { SDL_GAMEPAD_BUTTON_SOUTH, 1 }, { SDL_GAMEPAD_BUTTON_EAST, 2 }, { SDL_GAMEPAD_BUTTON_WEST, 4 },
                        { SDL_GAMEPAD_BUTTON_NORTH, 8 }, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x10 },
                        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x20 }, { SDL_GAMEPAD_BUTTON_BACK, 0x100 },
                        { SDL_GAMEPAD_BUTTON_START, 0x200 }, { SDL_GAMEPAD_BUTTON_DPAD_UP, 0x400 },
                        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x800 }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x1000 },
                        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x2000 } };

            for (b = 0; b < (int)(sizeof(BTN) / sizeof(BTN[0])); b++) {
                if (SDL_GetGamepadButton(d->gp, BTN[b].b)) {
                    v |= BTN[b].bit;
                }
            }
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8192 ? 0x40 : 0;
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8192 ? 0x80 : 0;
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_LEFTY) < -16384 ? 0x400 : 0;
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_LEFTX) > 16384 ? 0x800 : 0;
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_LEFTY) > 16384 ? 0x1000 : 0;
            v |= SDL_GetGamepadAxis(d->gp, SDL_GAMEPAD_AXIS_LEFTX) < -16384 ? 0x2000 : 0;
        } else {
            static const uint32_t JB[10] = { 4, 1, 2, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200 };
            Uint8 h = SDL_GetJoystickHat(d->joy, 0);

            for (b = 0; b < 10; b++) {
                v |= SDL_GetJoystickButton(d->joy, b) ? JB[b] : 0;
            }
            v |= (h & SDL_HAT_UP ? 0x400 : 0) | (h & SDL_HAT_RIGHT ? 0x800 : 0) | (h & SDL_HAT_DOWN ? 0x1000 : 0) |
                 (h & SDL_HAT_LEFT ? 0x2000 : 0);
            v |= SDL_GetJoystickAxis(d->joy, 1) < -16384 ? 0x400 : 0;
            v |= SDL_GetJoystickAxis(d->joy, 0) > 16384 ? 0x800 : 0;
            v |= SDL_GetJoystickAxis(d->joy, 1) > 16384 ? 0x1000 : 0;
            v |= SDL_GetJoystickAxis(d->joy, 0) < -16384 ? 0x2000 : 0;
        }
    }
    return v;
}

int main(int argc, char **argv)
{
    PihAdapter a;

    memset(&a, 0, sizeof(a));
    a.name = "pih-selftest";
    a.open = st_open;
    a.close = st_close;
    a.pump = st_pump;
    a.read = st_read;
    a.players = 2;
    a.third_shares_first = 1;
    return pih_main(&a, argc, argv);
}
