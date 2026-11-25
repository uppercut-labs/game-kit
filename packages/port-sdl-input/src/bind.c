/* Bindings as text (bind.h). */
#include "bind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the gamepad buttons by position; SDL's own names ("a", "dpup") are read too */
static const struct {
    const char *name;
    SDL_GamepadButton b;
} PAD_NAMES[] = {
    { "south", SDL_GAMEPAD_BUTTON_SOUTH },
    { "east", SDL_GAMEPAD_BUTTON_EAST },
    { "west", SDL_GAMEPAD_BUTTON_WEST },
    { "north", SDL_GAMEPAD_BUTTON_NORTH },
    /* the back buttons, as the Steam Deck and Steam Controller label them */
    { "r4", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 },
    { "l4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
    { "r5", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 },
    { "l5", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
};

static const struct {
    const char *name;
    int hat;
} HAT_NAMES[] = { { "up", SDL_HAT_UP }, { "right", SDL_HAT_RIGHT }, { "down", SDL_HAT_DOWN }, { "left", SDL_HAT_LEFT } };

int bind_class(const Bind *b)
{
    return b->kind == B_KEY ? C_KEY : (b->kind == B_PAD_BUTTON || b->kind == B_PAD_AXIS) ? C_PAD : C_JOY;
}

/* "sc42": a key by its scancode number, for the keys whose names the file cannot hold (",", "Non-US #") */
static int parse_scancode_number(const char *s, Bind *out)
{
    char *end;
    long n;

    if (s[0] != 's' || s[1] != 'c' || s[2] < '0' || s[2] > '9') {
        return 0;
    }
    n = strtol(s + 2, &end, 10);
    if (*end || n <= 0 || n >= SDL_SCANCODE_COUNT) {
        return 0;
    }
    out->kind = B_KEY;
    out->code = (int16_t)n;
    return 1;
}

int bind_parse(const char *s, Bind *out)
{
    char *end;
    long n;
    size_t i;

    memset(out, 0, sizeof(*out));
    if (!strncmp(s, "key:", 4)) {
        SDL_Scancode k;

        if (parse_scancode_number(s + 4, out)) {
            return 0;
        }
        k = SDL_GetScancodeFromName(s + 4);
        if (k == SDL_SCANCODE_UNKNOWN) {
            return -1;
        }
        out->kind = B_KEY;
        out->code = (int16_t)k;
        return 0;
    }
    if (!strncmp(s, "pad:", 4)) {
        const char *p = s + 4;
        size_t len = strlen(p);
        char axis[32];

        for (i = 0; i < sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0]); i++) {
            if (!strcmp(p, PAD_NAMES[i].name)) {
                out->kind = B_PAD_BUTTON;
                out->code = (int16_t)PAD_NAMES[i].b;
                return 0;
            }
        }
        if (len > 1 && len < sizeof(axis) && (p[len - 1] == '+' || p[len - 1] == '-')) {
            SDL_GamepadAxis a;

            memcpy(axis, p, len - 1);
            axis[len - 1] = 0;
            a = SDL_GetGamepadAxisFromString(axis);
            if (a == SDL_GAMEPAD_AXIS_INVALID) {
                return -1;
            }
            out->kind = B_PAD_AXIS;
            out->code = (int16_t)a;
            out->dir = p[len - 1] == '+' ? 1 : -1;
            return 0;
        }
        {
            SDL_GamepadButton b = SDL_GetGamepadButtonFromString(p);

            if (b == SDL_GAMEPAD_BUTTON_INVALID) {
                return -1;
            }
            out->kind = B_PAD_BUTTON;
            out->code = (int16_t)b;
            return 0;
        }
    }
    if (!strncmp(s, "joy:", 4) && (s[4] == 'b' || s[4] == 'h' || s[4] == 'a')) {
        n = strtol(s + 5, &end, 10);
        if (end == s + 5 || n < 0 || n > 255) {
            return -1;
        }
        out->code = (int16_t)n;
        if (s[4] == 'b' && !*end) {
            out->kind = B_JOY_BUTTON;
            return 0;
        }
        if (s[4] == 'a' && (end[0] == '+' || end[0] == '-') && !end[1]) {
            out->kind = B_JOY_AXIS;
            out->dir = end[0] == '+' ? 1 : -1;
            return 0;
        }
        if (s[4] == 'h' && end[0] == '.') {
            for (i = 0; i < 4; i++) {
                if (!strcmp(end + 1, HAT_NAMES[i].name)) {
                    out->kind = B_JOY_HAT;
                    out->dir = (int8_t)HAT_NAMES[i].hat;
                    return 0;
                }
            }
        }
    }
    return -1;
}

void bind_format(const Bind *b, char *out, size_t cap)
{
    size_t i;

    switch (b->kind) {
    case B_KEY: {
        const char *name = SDL_GetScancodeName((SDL_Scancode)b->code);

        /* a name with a comma or a '#' would split or cut the line: write the key by its number */
        if (!name || !*name || strpbrk(name, ",#=") || name[0] == ' ' || name[strlen(name) - 1] == ' ') {
            snprintf(out, cap, "key:sc%d", b->code);
        } else {
            snprintf(out, cap, "key:%s", name);
        }
        return;
    }
    case B_PAD_BUTTON:
        for (i = 0; i < sizeof(PAD_NAMES) / sizeof(PAD_NAMES[0]); i++) {
            if (PAD_NAMES[i].b == (SDL_GamepadButton)b->code) {
                snprintf(out, cap, "pad:%s", PAD_NAMES[i].name);
                return;
            }
        }
        snprintf(out, cap, "pad:%s", SDL_GetGamepadStringForButton((SDL_GamepadButton)b->code));
        return;
    case B_PAD_AXIS:
        snprintf(out, cap, "pad:%s%c", SDL_GetGamepadStringForAxis((SDL_GamepadAxis)b->code), b->dir > 0 ? '+' : '-');
        return;
    case B_JOY_BUTTON:
        snprintf(out, cap, "joy:b%d", b->code);
        return;
    case B_JOY_AXIS:
        snprintf(out, cap, "joy:a%d%c", b->code, b->dir > 0 ? '+' : '-');
        return;
    case B_JOY_HAT:
        for (i = 0; i < 4; i++) {
            if (HAT_NAMES[i].hat == b->dir) {
                snprintf(out, cap, "joy:h%d.%s", b->code, HAT_NAMES[i].name);
                return;
            }
        }
        break;
    }
    snprintf(out, cap, "none");
}

void bind_describe(const Bind *b, char *out, size_t cap)
{
    static const char *const POS[] = { "south", "east", "west", "north" };

    switch (b->kind) {
    case B_KEY:
        snprintf(out, cap, "%s key", SDL_GetScancodeName((SDL_Scancode)b->code));
        return;
    case B_PAD_BUTTON:
        switch (b->code) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_WEST:
        case SDL_GAMEPAD_BUTTON_NORTH:
            snprintf(out, cap, "%s button", POS[b->code - SDL_GAMEPAD_BUTTON_SOUTH]);
            return;
        case SDL_GAMEPAD_BUTTON_BACK: snprintf(out, cap, "Create/Back"); return;
        case SDL_GAMEPAD_BUTTON_START: snprintf(out, cap, "Options/Start"); return;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: snprintf(out, cap, "pad L1"); return;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: snprintf(out, cap, "pad R1"); return;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: snprintf(out, cap, "pad L3"); return;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: snprintf(out, cap, "pad R3"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: snprintf(out, cap, "d-pad up"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: snprintf(out, cap, "d-pad down"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: snprintf(out, cap, "d-pad left"); return;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: snprintf(out, cap, "d-pad right"); return;
        default: break;
        }
        break;
    case B_PAD_AXIS:
        switch (b->code) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: snprintf(out, cap, "pad L2"); return;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: snprintf(out, cap, "pad R2"); return;
        case SDL_GAMEPAD_AXIS_LEFTX: snprintf(out, cap, "left stick %s", b->dir > 0 ? "right" : "left"); return;
        case SDL_GAMEPAD_AXIS_LEFTY: snprintf(out, cap, "left stick %s", b->dir > 0 ? "down" : "up"); return;
        case SDL_GAMEPAD_AXIS_RIGHTX: snprintf(out, cap, "right stick %s", b->dir > 0 ? "right" : "left"); return;
        case SDL_GAMEPAD_AXIS_RIGHTY: snprintf(out, cap, "right stick %s", b->dir > 0 ? "down" : "up"); return;
        default: break;
        }
        break;
    case B_JOY_BUTTON:
        snprintf(out, cap, "stick button %d", b->code + 1);
        return;
    case B_JOY_HAT:
    case B_JOY_AXIS:
        snprintf(out, cap, "stick %s", b->dir > 0 && b->kind == B_JOY_AXIS ? "+" : "direction");
        return;
    }
    bind_format(b, out, cap);
}
