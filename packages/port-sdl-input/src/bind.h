/* Bindings as text (internal). */
#ifndef PSI_BIND_H
#define PSI_BIND_H

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

enum { B_NONE, B_KEY, B_PAD_BUTTON, B_PAD_AXIS, B_JOY_BUTTON, B_JOY_HAT, B_JOY_AXIS };
/* what a remap replaces: a slot's bindings of the same class */
enum { C_KEY, C_PAD, C_JOY };

typedef struct {
    uint8_t kind;
    int8_t dir;        /* an axis' sign; a hat's SDL_HAT_* bit */
    int16_t code;      /* scancode, gamepad button or axis, joystick button, hat or axis */
} Bind;

int bind_class(const Bind *b);
/* "key:Z", "pad:south", "pad:lefttrigger+", "joy:b3", "joy:h0.up", "joy:a1-": 0, or -1 if it is not one */
int bind_parse(const char *s, Bind *out);
void bind_format(const Bind *b, char *out, size_t cap);
/* what a person reads for one binding ("south button", "Z key", "pad L1": PlayStation names for shoulders, triggers, clicks) */
void bind_describe(const Bind *b, char *out, size_t cap);

#endif
