/* One pad's state as the input layer hands it to a port. Nothing here depends on SDL.
 *
 * buttons is a word of the port's own bits: the port's layout (layout.h) says which bit each button is, so a
 * PlayStation 1 word, a PlayStation 2 word or any other 32-bit scheme works. Bits are active high (1 = held); a
 * port that wants the word active low inverts it.
 */
#ifndef PSI_PAD_H
#define PSI_PAD_H

#include <stdint.h>

#define PSI_STICK_CENTRE 0x80

typedef struct {
    uint32_t buttons;               /* the port's bits, active high */
    uint8_t lx, ly, rx, ry;         /* sticks, 0x80 centred; read only while analog is set */
    uint8_t analog;                 /* 1: the player's pad is in analog mode */
} PsiPad;

/* Nothing pressed, sticks centred, digital. */
void psi_pad_clear(PsiPad *p);
int psi_pad_equal(const PsiPad *a, const PsiPad *b);

/* Simultaneous opposite directions (SOCD): left and right together cancel, up and down together give up (what a
   hitbox controller does and a d-pad cannot press). */
uint32_t psi_socd_clean(uint32_t buttons, uint32_t up, uint32_t down, uint32_t left, uint32_t right);

/* A device with no stick (a key, a d-pad, a hat) still steers in analog mode: while the left stick is centred,
   the held directions move it to its ends. */
void psi_steer_stick(PsiPad *p, uint32_t up, uint32_t down, uint32_t left, uint32_t right);

/* Swapping two buttons wherever the port asks (confirm and cancel in menus, say). A press keeps the meaning it had
   when it went down until it is released, so a button held across a change of screen does not turn into a new
   press of the other one. */
typedef struct {
    int8_t held[2];   /* -1 up, 0 held as itself, 1 held swapped */
} PsiSwapLatch;

void psi_swap_reset(PsiSwapLatch *l);
/* One field's buttons with a and b (single bits) swapped for the presses that went down while swap was on. */
uint32_t psi_swap_apply(PsiSwapLatch *l, uint32_t buttons, uint32_t a, uint32_t b, int swap);

#endif
