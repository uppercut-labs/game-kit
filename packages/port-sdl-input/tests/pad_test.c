/* psi-pad-test: the parts of the input layer that need no SDL (pad.h, family.h, layout.h's PlayStation table). */
#include <stdio.h>
#include <string.h>

#include "psi/family.h"
#include "psi/layout.h"
#include "psi/pad.h"

static int checks, failures;

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

enum { UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, A = 16, B = 32 };

int main(void)
{
    PsiPad a, b;
    PsiSwapLatch l;
    PsiSlot slots[PSI_PS_COUNT];
    uint32_t masks[PSI_PS_COUNT];
    int i, seen[PSI_PS_COUNT] = { 0 };

    /* the pad word */
    psi_pad_clear(&a);
    psi_pad_clear(&b);
    CHECK(a.buttons == 0 && a.lx == 0x80 && a.ry == 0x80 && !a.analog, "cleared pad");
    CHECK(psi_pad_equal(&a, &b), "two cleared pads are equal");
    b.rx = 0x81;
    CHECK(!psi_pad_equal(&a, &b), "a stick differs");
    psi_pad_clear(&b);
    b.analog = 1;
    CHECK(!psi_pad_equal(&a, &b), "the mode differs");

    /* SOCD */
    CHECK(psi_socd_clean(LEFT | RIGHT | A, UP, DOWN, LEFT, RIGHT) == A, "left+right cancel");
    CHECK(psi_socd_clean(UP | DOWN | A, UP, DOWN, LEFT, RIGHT) == (UP | A), "up+down gives up");
    CHECK(psi_socd_clean(UP | LEFT | A, UP, DOWN, LEFT, RIGHT) == (UP | LEFT | A), "a diagonal is not touched");
    CHECK(psi_socd_clean(LEFT | RIGHT | UP | DOWN, UP, DOWN, LEFT, RIGHT) == UP, "both pairs");
    CHECK(psi_socd_clean(A | B, 0, 0, 0, 0) == (A | B), "a layout with no directions is not touched");

    /* a key steers the centred stick, and only while it is centred */
    psi_pad_clear(&a);
    a.buttons = LEFT | UP;
    psi_steer_stick(&a, UP, DOWN, LEFT, RIGHT);
    CHECK(a.lx == 0x00 && a.ly == 0x00, "left and up: %02x %02x", a.lx, a.ly);
    psi_pad_clear(&a);
    a.buttons = RIGHT | DOWN;
    psi_steer_stick(&a, UP, DOWN, LEFT, RIGHT);
    CHECK(a.lx == 0xFF && a.ly == 0xFF, "right and down: %02x %02x", a.lx, a.ly);
    psi_pad_clear(&a);
    a.buttons = LEFT;
    a.lx = 0x20;
    a.ly = 0x80;
    psi_steer_stick(&a, UP, DOWN, LEFT, RIGHT);
    CHECK(a.lx == 0x20, "a stick that is already off centre is left alone: %02x", a.lx);

    /* the swap: a press keeps the meaning it had when it went down */
    psi_swap_reset(&l);
    CHECK(psi_swap_apply(&l, A, A, B, 0) == A, "swap off: A is A");
    CHECK(psi_swap_apply(&l, 0, A, B, 1) == 0, "released");
    CHECK(psi_swap_apply(&l, A, A, B, 1) == B, "swap on: A is B");
    CHECK(psi_swap_apply(&l, B, A, B, 1) == A, "swap on: B is A");
    CHECK(psi_swap_apply(&l, A | B, A, B, 1) == (A | B), "both held: both");
    psi_swap_reset(&l);
    CHECK(psi_swap_apply(&l, A, A, B, 1) == B, "pressed with swap on");
    CHECK(psi_swap_apply(&l, A, A, B, 0) == B, "the swap turns off while held: still B until released");
    CHECK(psi_swap_apply(&l, 0, A, B, 0) == 0 && psi_swap_apply(&l, A, A, B, 0) == A, "a new press takes the new meaning");
    CHECK(psi_swap_apply(&l, A | UP, A, B, 1) == (A | UP), "other buttons pass through");

    /* the family names */
    CHECK(!strcmp(psi_family_name(PSI_FAMILY_NINTENDO), "nintendo") && !strcmp(psi_family_name((PsiFamily)99), "other"),
          "names");
    CHECK(psi_family_parse("steam") == PSI_FAMILY_STEAM && psi_family_parse("keyboard") == PSI_FAMILY_KEYBOARD &&
              psi_family_parse("auto") == -1 && psi_family_parse("") == -1,
          "parse");

    /* the PlayStation slot table: every button once, with the masks the port gave */
    for (i = 0; i < PSI_PS_COUNT; i++) {
        masks[i] = 0x100u << i;
    }
    psi_playstation_slots(slots, masks);
    for (i = 0; i < PSI_PS_COUNT; i++) {
        CHECK(slots[i].mask == masks[i] && slots[i].name && slots[i].key && slots[i].defaults, "slot %d", i);
        CHECK(!strcmp(slots[i].key, slots[PSI_PS_X].key) == (i == PSI_PS_X), "keys are unique: %s", slots[i].key);
    }
    CHECK(!strcmp(slots[PSI_PS_X].name, "X") && !strcmp(slots[PSI_PS_X].key, "x") &&
              !strcmp(slots[PSI_PS_X].old_key, "cross") && !strcmp(slots[PSI_PS_START].name, "Start"),
          "X is X, and was once Cross");
    CHECK(strstr(slots[PSI_PS_X].defaults, "pad:south") && strstr(slots[PSI_PS_CIRCLE].defaults, "pad:east") &&
              strstr(slots[PSI_PS_SQUARE].defaults, "pad:west") && strstr(slots[PSI_PS_TRIANGLE].defaults, "pad:north"),
          "the face buttons by position");
    for (i = 0; i < psi_playstation_remap_count; i++) {
        CHECK(psi_playstation_remap_order[i] >= 0 && psi_playstation_remap_order[i] < PSI_PS_COUNT &&
                  !seen[psi_playstation_remap_order[i]]++,
              "the remap order lists each slot once: %d", psi_playstation_remap_order[i]);
    }
    CHECK(psi_playstation_remap_count == PSI_PS_COUNT, "and all of them");

    printf("psi-pad-test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
