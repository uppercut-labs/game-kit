/* ps2-multitap-pads: what one poll gives for one controller, the DualShock 2 button bits and their names. Part of
 * ps2-multitap-pads 1.1.0 (MIT).
 *
 * This header needs only <stdint.h>, so code that shares a PS2 game's input handling with another console's build (a
 * pause menu, a replay reader, a host test) can use the sample and the bits without the PS2SDK. mpad/mpad.h includes it.
 */
#ifndef MPAD_SAMPLE_H
#define MPAD_SAMPLE_H

#include <stdint.h>

/* The DualShock 2 buttons in a sample's `held`, one bit each, set while pressed (the hardware reports them the other
 * way round). The values are the PS2SDK's PAD_* bits; src/ps2.c checks that at compile time. */
enum {
    MPAD_SELECT = 0x0001,
    MPAD_L3 = 0x0002,
    MPAD_R3 = 0x0004,
    MPAD_START = 0x0008,
    MPAD_UP = 0x0010,
    MPAD_RIGHT = 0x0020,
    MPAD_DOWN = 0x0040,
    MPAD_LEFT = 0x0080,
    MPAD_L2 = 0x0100,
    MPAD_R2 = 0x0200,
    MPAD_L1 = 0x0400,
    MPAD_R1 = 0x0800,
    MPAD_TRIANGLE = 0x1000,
    MPAD_CIRCLE = 0x2000,
    MPAD_CROSS = 0x4000,
    MPAD_SQUARE = 0x8000,
    MPAD_X = MPAD_CROSS /* the face button's printed name; the SDK calls the bit PAD_CROSS */
};

/* What a poll says about one endpoint. */
enum {
    MPAD_ABSENT = 0, /* nothing there (or never opened): buttons none, sticks centred */
    MPAD_FRESH = 1, /* a pad was read this frame: `held` and the sticks are real */
    MPAD_UNREADABLE = 2 /* a pad is there but gave no sample this frame (still being found, switching mode): neutral */
};

typedef struct MpadSample {
    uint16_t held;
    uint8_t lx; /* left and right sticks, 0..255, centre 128; centred unless the pad is analog */
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t status; /* MPAD_ABSENT, MPAD_FRESH or MPAD_UNREADABLE */
} MpadSample;

/*
 * Every button once, in bit order, as X(name, bit): for a table of names, a remap screen or a log. The names are the
 * ones printed on the pad and in a replay or capture script ("X", not "Cross"). mpad/padr.h turns this into a
 * pad-replay name table.
 */
#define MPAD_FOR_EACH_BUTTON(X) \
    X("Select", MPAD_SELECT)    \
    X("L3", MPAD_L3)            \
    X("R3", MPAD_R3)            \
    X("Start", MPAD_START)      \
    X("Up", MPAD_UP)            \
    X("Right", MPAD_RIGHT)      \
    X("Down", MPAD_DOWN)        \
    X("Left", MPAD_LEFT)        \
    X("L2", MPAD_L2)            \
    X("R2", MPAD_R2)            \
    X("L1", MPAD_L1)            \
    X("R1", MPAD_R1)            \
    X("Triangle", MPAD_TRIANGLE) \
    X("Circle", MPAD_CIRCLE)    \
    X("X", MPAD_X)              \
    X("Square", MPAD_SQUARE)

#define MPAD_BUTTON_COUNT 16

/*
 * Short names, opt-in because they are short enough to collide with a project's own: define MPAD_SHORT_NAMES before the
 * first include. B_X and the rest are the button bits; INPUT_ABSENT, INPUT_FRESH and INPUT_UNREADABLE are the statuses.
 * They are macros for the MPAD_ names themselves, so the two sets mix freely (in a switch, a comparison or a mask).
 */
#ifdef MPAD_SHORT_NAMES
#define B_SELECT MPAD_SELECT
#define B_L3 MPAD_L3
#define B_R3 MPAD_R3
#define B_START MPAD_START
#define B_UP MPAD_UP
#define B_RIGHT MPAD_RIGHT
#define B_DOWN MPAD_DOWN
#define B_LEFT MPAD_LEFT
#define B_L2 MPAD_L2
#define B_R2 MPAD_R2
#define B_L1 MPAD_L1
#define B_R1 MPAD_R1
#define B_TRIANGLE MPAD_TRIANGLE
#define B_CIRCLE MPAD_CIRCLE
#define B_X MPAD_X
#define B_SQUARE MPAD_SQUARE
#define INPUT_ABSENT MPAD_ABSENT
#define INPUT_FRESH MPAD_FRESH
#define INPUT_UNREADABLE MPAD_UNREADABLE
#endif

#endif
