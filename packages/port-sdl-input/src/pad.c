/* The parts of the input layer that need no SDL (pad.h). */
#include "psi/pad.h"

#include <string.h>

void psi_pad_clear(PsiPad *p)
{
    p->buttons = 0;
    p->lx = p->ly = p->rx = p->ry = PSI_STICK_CENTRE;
    p->analog = 0;
}

int psi_pad_equal(const PsiPad *a, const PsiPad *b)
{
    return a->buttons == b->buttons && a->analog == b->analog && a->lx == b->lx && a->ly == b->ly && a->rx == b->rx &&
           a->ry == b->ry;
}

uint32_t psi_socd_clean(uint32_t buttons, uint32_t up, uint32_t down, uint32_t left, uint32_t right)
{
    if (left && right && (buttons & (left | right)) == (left | right)) {
        buttons &= ~(left | right);
    }
    if (up && down && (buttons & (up | down)) == (up | down)) {
        buttons &= ~down;
    }
    return buttons;
}

void psi_steer_stick(PsiPad *p, uint32_t up, uint32_t down, uint32_t left, uint32_t right)
{
    if (p->lx > 0x60 && p->lx < 0xA0 && p->ly > 0x60 && p->ly < 0xA0) {
        if (left && (p->buttons & left)) {
            p->lx = 0x00;
        } else if (right && (p->buttons & right)) {
            p->lx = 0xFF;
        }
        if (up && (p->buttons & up)) {
            p->ly = 0x00;
        } else if (down && (p->buttons & down)) {
            p->ly = 0xFF;
        }
    }
}

void psi_swap_reset(PsiSwapLatch *l)
{
    l->held[0] = l->held[1] = -1;
}

uint32_t psi_swap_apply(PsiSwapLatch *l, uint32_t buttons, uint32_t a, uint32_t b, int swap)
{
    const uint32_t bit[2] = { a, b };
    uint32_t out = buttons & ~(a | b);
    int i;

    for (i = 0; i < 2; i++) {
        if (!(buttons & bit[i])) {
            l->held[i] = -1;
            continue;
        }
        if (l->held[i] < 0) {
            l->held[i] = (int8_t)(swap != 0);
        }
        out |= l->held[i] ? bit[1 - i] : bit[i];
    }
    return out;
}
