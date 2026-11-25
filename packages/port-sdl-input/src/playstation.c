/* Ready-made slots for the sixteen buttons of a PlayStation pad (layout.h). */
#include <string.h>

#include "psi/layout.h"

/* the order a person thinks of them in: directions, then face buttons, shoulders, Start, Select, stick clicks */
const int psi_playstation_remap_order[PSI_PS_COUNT] = {
    PSI_PS_UP, PSI_PS_DOWN, PSI_PS_LEFT, PSI_PS_RIGHT, PSI_PS_SQUARE, PSI_PS_X, PSI_PS_TRIANGLE, PSI_PS_CIRCLE,
    PSI_PS_L1, PSI_PS_R1, PSI_PS_L2, PSI_PS_R2, PSI_PS_START, PSI_PS_SELECT, PSI_PS_L3, PSI_PS_R3,
};
const int psi_playstation_remap_count = PSI_PS_COUNT;

void psi_playstation_slots(PsiSlot out[PSI_PS_COUNT], const uint32_t masks[PSI_PS_COUNT])
{
    static const struct {
        const char *name, *key, *old_key, *defaults;
    } S[PSI_PS_COUNT] = {
        { "Select", "select", NULL, "key:Right Shift, pad:back, joy:b8" },
        { "L3", "l3", NULL, "pad:leftstick, joy:b10" },
        { "R3", "r3", NULL, "pad:rightstick, joy:b11" },
        { "Start", "start", NULL, "key:Return, pad:start, joy:b9" },
        { "Up", "up", NULL, "key:Up, pad:dpup, pad:lefty-, joy:h0.up, joy:a1-" },
        { "Right", "right", NULL, "key:Right, pad:dpright, pad:leftx+, joy:h0.right, joy:a0+" },
        { "Down", "down", NULL, "key:Down, pad:dpdown, pad:lefty+, joy:h0.down, joy:a1+" },
        { "Left", "left", NULL, "key:Left, pad:dpleft, pad:leftx-, joy:h0.left, joy:a0-" },
        { "L2", "l2", NULL, "key:E, pad:lefttrigger+, pad:l5, joy:b6" },
        { "R2", "r2", NULL, "key:R, pad:righttrigger+, pad:r5, joy:b7" },
        { "L1", "l1", NULL, "key:Q, pad:leftshoulder, pad:l4, joy:b4" },
        { "R1", "r1", NULL, "key:W, pad:rightshoulder, pad:r4, joy:b5" },
        { "Triangle", "triangle", NULL, "key:A, pad:north, joy:b3" },
        { "Circle", "circle", NULL, "key:S, pad:east, joy:b2" },
        { "X", "x", "cross", "key:X, pad:south, joy:b1" },
        { "Square", "square", NULL, "key:Z, pad:west, joy:b0" },
    };
    int i;

    for (i = 0; i < PSI_PS_COUNT; i++) {
        out[i].name = S[i].name;
        out[i].key = S[i].key;
        out[i].old_key = S[i].old_key;
        out[i].mask = masks[i];
        out[i].defaults = S[i].defaults;
    }
}
