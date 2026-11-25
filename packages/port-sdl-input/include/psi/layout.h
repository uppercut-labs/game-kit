/* What the port supplies: its buttons, as data. The input layer knows no game; a slot is whatever the port says it
 * is, and has a bit in the port's pad word (pad.h). Nothing here depends on SDL.
 */
#ifndef PSI_LAYOUT_H
#define PSI_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define PSI_PLAYERS 2
#define PSI_MAX_SLOTS 32

/* One thing a player can bind: a button, or an extra such as a macro. */
typedef struct {
    const char *name;       /* what a person reads, and the remap prompt asks for: "X", "Start", "P AND K" */
    const char *key;        /* the settings key's end: "p1.<key>", lower case: "x", "start", "p_and_k" */
    const char *old_key;    /* an earlier key still read when <key> is not set, or NULL */
    uint32_t mask;          /* the bit or bits it gives in the pad word; 0: the port sets it later (psi_set_slot_mask) */
    const char *defaults;   /* the default bindings as text: "key:Z, pad:south, joy:b1"; NULL or "none": no binding */
} PsiSlot;

typedef struct {
    const PsiSlot *slots;
    int nslots;                                 /* 1 to PSI_MAX_SLOTS */
    uint32_t up, down, left, right;             /* the direction bits: SOCD and stick steering use them (0: none) */
    const int *remap_order;                     /* the slots in the order a remap run asks for them, or NULL: slot order */
    int nremap;
    uint32_t swap_a, swap_b;                    /* the two bits psi_set_swap exchanges (0: none) */
} PsiLayout;

/* Where the settings live: the three functions of port-settings (pset_store_get, pset_store_set, pset_store_save),
   or any other store. get returns NULL for a key that is not set; set and save may be NULL. */
typedef struct {
    void *ctx;
    const char *(*get)(void *ctx, const char *key);
    void (*set)(void *ctx, const char *key, const char *value);
    int (*save)(void *ctx);                     /* 0 on success */
} PsiStore;

/* Ready-made slots for the sixteen buttons of a PlayStation pad, to take as they are or change. */
enum {
    PSI_PS_SELECT, PSI_PS_L3, PSI_PS_R3, PSI_PS_START, PSI_PS_UP, PSI_PS_RIGHT, PSI_PS_DOWN, PSI_PS_LEFT,
    PSI_PS_L2, PSI_PS_R2, PSI_PS_L1, PSI_PS_R1, PSI_PS_TRIANGLE, PSI_PS_CIRCLE, PSI_PS_X, PSI_PS_SQUARE,
    PSI_PS_COUNT
};

/* Fills out[PSI_PS_COUNT] with the buttons' names, keys ("x", "circle"...) and default bindings by position
   (south X, east Circle, west Square, north Triangle, shoulders, triggers, d-pad and left stick for the directions,
   back Select, start Start, stick clicks L3 and R3, the back paddles on L1 R1 L2 R2; an arcade stick in DirectInput
   order). masks[i] is the pad-word bit the port gives button i. A port changes a slot's defaults by assigning to
   its fields afterwards. */
void psi_playstation_slots(PsiSlot out[PSI_PS_COUNT], const uint32_t masks[PSI_PS_COUNT]);
/* The order a person thinks of them in: the directions, then the face buttons, shoulders, Start and Select. */
extern const int psi_playstation_remap_order[];
extern const int psi_playstation_remap_count;

#endif
