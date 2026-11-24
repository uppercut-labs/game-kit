/* Virtual controllers (pih.h). */
#include <stdlib.h>
#include <string.h>

#include "pih/pih.h"

struct PihDevice {
    const PihProfile *profile;
    SDL_JoystickID id;
    SDL_Joystick *joy;
    unsigned rumble_calls;
    int rumble_low, rumble_high;
    int hat_held[4];
};

static bool SDLCALL on_rumble(void *ud, Uint16 lo, Uint16 hi)
{
    PihDevice *d = ud;

    d->rumble_calls++;
    d->rumble_low = lo;
    d->rumble_high = hi;
    return true;
}

static void rest_axes(PihDevice *d)
{
    int i;

    for (i = 0; i < d->profile->ncontrols; i++) {
        const PihControl *c = &d->profile->controls[i];

        if (c->type == PIH_AXIS) {
            SDL_SetJoystickVirtualAxis(d->joy, c->index, (Sint16)c->rest);
        }
    }
}

PihDevice *pih_attach(const PihProfile *p)
{
    SDL_VirtualJoystickDesc desc;
    PihDevice *d = calloc(1, sizeof(*d));

    if (!d) {
        return NULL;
    }
    d->profile = p;
    SDL_INIT_INTERFACE(&desc);
    desc.vendor_id = p->vendor;
    desc.product_id = p->product;
    desc.name = p->device_name[0] ? p->device_name : p->name;
    desc.userdata = d;
    desc.Rumble = on_rumble;
    desc.type = p->joytype;
    if (p->gamepad) {
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.button_mask = 0x1fffff;   /* the standard buttons, the paddles and the touchpad */
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    } else {
        desc.naxes = (Uint16)p->naxes;
        desc.nbuttons = (Uint16)p->nbuttons;
        desc.nhats = (Uint16)p->nhats;
    }
    d->id = SDL_AttachVirtualJoystick(&desc);
    if (!d->id) {
        free(d);
        return NULL;
    }
    d->joy = SDL_OpenJoystick(d->id);
    if (!d->joy) {
        SDL_DetachVirtualJoystick(d->id);
        free(d);
        return NULL;
    }
    rest_axes(d);
    return d;
}

void pih_detach(PihDevice *d)
{
    if (!d) {
        return;
    }
    SDL_CloseJoystick(d->joy);
    SDL_DetachVirtualJoystick(d->id);
    free(d);
}

const PihProfile *pih_profile(const PihDevice *d)
{
    return d->profile;
}

SDL_JoystickID pih_id(const PihDevice *d)
{
    return d->id;
}

SDL_Joystick *pih_joystick(const PihDevice *d)
{
    return d->joy;
}

void pih_set_control(PihDevice *d, const PihControl *c, int down)
{
    switch (c->type) {
    case PIH_BUTTON:
        SDL_SetJoystickVirtualButton(d->joy, c->index, down != 0);
        break;
    case PIH_AXIS:
        SDL_SetJoystickVirtualAxis(d->joy, c->index, (Sint16)(down ? c->value : c->rest));
        break;
    case PIH_HAT: {
        int *h = &d->hat_held[c->index & 3];

        *h = down ? (*h | c->value) : (*h & ~c->value);
        SDL_SetJoystickVirtualHat(d->joy, c->index, (Uint8)*h);
        break;
    }
    }
}

int pih_press(PihDevice *d, const char *control, int down)
{
    const PihControl *c = pih_find_control(d->profile, control);

    if (!c) {
        return 0;
    }
    pih_set_control(d, c, down);
    return 1;
}

void pih_release_all(PihDevice *d)
{
    int i;

    for (i = 0; i < d->profile->ncontrols; i++) {
        pih_set_control(d, &d->profile->controls[i], 0);
    }
}

void pih_stick(PihDevice *d, int which, uint8_t x, uint8_t y)
{
    int ax = d->profile->gamepad ? (which ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX) : 2 * (which ? 1 : 0);

    SDL_SetJoystickVirtualAxis(d->joy, ax, (Sint16)(x * 256 - 32768));
    SDL_SetJoystickVirtualAxis(d->joy, ax + 1, (Sint16)(y * 256 - 32768));
}

void pih_raw_axis(PihDevice *d, int axis, int value)
{
    SDL_SetJoystickVirtualAxis(d->joy, axis, (Sint16)value);
}

unsigned pih_rumble_calls(const PihDevice *d)
{
    return d->rumble_calls;
}

unsigned pih_rumble_last(const PihDevice *d, int *low, int *high)
{
    if (low) {
        *low = d->rumble_low;
    }
    if (high) {
        *high = d->rumble_high;
    }
    return d->rumble_calls;
}
