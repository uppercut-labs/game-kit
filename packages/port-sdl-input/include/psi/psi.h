/* port-sdl-input: a game port's input layer on SDL3.
 *
 * Every keyboard, gamepad and joystick (an arcade stick) is open at once, and a port asks for its pads once a frame.
 *
 * - Players. Each player has bindings for each slot of the port's layout (layout.h): keys, which belong to that
 *   player, and gamepad or joystick inputs, which apply to the devices assigned to that player. The keyboard and the
 *   on-screen controller (psi_set_touch) are player 1's. A device goes to player 1 when it connects, then player 2; a
 *   third and later go to player 1 too. "pN.device = <SDL GUID>" pins one to player N; psi_swap_players swaps the
 *   two players' devices. Devices already connected when the layer starts are opened.
 * - Gamepads map by position, so a DualSense, a DualShock 4, an Xbox pad, a Switch Pro and the Steam Deck's own
 *   controls all map the same way with no setup: SDL's south, east, west and north buttons, shoulders, triggers
 *   (past a quarter), d-pad, left stick (past half way), back, start and stick clicks, and the Deck's back buttons
 *   (L4 R4 L5 R5). A joystick SDL has no gamepad mapping for (many arcade sticks) uses the DirectInput order: buttons
 *   0-9, the first hat and the first two axes. Which slot gets which input is the layout's default bindings.
 * - Bindings are text: "key:Z, pad:south, pad:lefttrigger+, pad:l4, joy:b1, joy:h0.up, joy:a0-" in the settings key
 *   "p1.x". An input of a kind (keyboard, gamepad, joystick) set by a remap replaces that slot's binding of the same
 *   kind and keeps the others.
 * - SOCD: "socd = hitbox" (the default) cancels left+right and gives up for up+down; "socd = off" passes them on.
 * - Analog: a player's pad is digital unless "pN.analog = on" (psi_set_analog); then the sticks are read, and a device
 *   without a stick steers the left stick.
 * - Remap capture: psi_remap_begin takes the next key, gamepad button or axis, or joystick button, hat or axis for
 *   one slot; psi_remap_start asks for each slot in turn. The result is saved in the store as text.
 * - The controller family (for button prompts): the family of the last device used, live, or "prompts = <family>".
 * - Rumble: psi_rumble, on the player's gamepads, unless "rumble = off".
 * - Extras: slots whose bit the port sets while it runs (macros that press the port's own buttons, psi_set_slot_mask),
 *   and a swap of two buttons while the port says so (confirm and cancel in menus, psi_set_swap).
 *
 * Settings keys read: pN.<slot key>, pN.analog, pN.device, socd, rumble, prompts. Written: pN.<slot key>, pN.analog.
 * Nothing here knows a game. SDL must be initialised with SDL_INIT_GAMEPAD (and events).
 */
#ifndef PSI_H
#define PSI_H

#include <stdio.h>

#include <SDL3/SDL.h>

#include "psi/family.h"
#include "psi/layout.h"
#include "psi/pad.h"

#define PSI_VERSION "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsiInput PsiInput;

typedef struct {
    const PsiLayout *layout;    /* required; the slots and their strings must outlive the layer */
    PsiStore store;             /* where bindings and settings live; all NULL: the layout's defaults, nothing saved */
    const char *name;           /* prefix of the log lines ("myport"); default "input" */
    /* where log lines go instead of stderr (one line, no newline) */
    void (*log)(void *user, const char *line);
    void *log_user;
    int only_virtual;           /* count only SDL's virtual controllers: for tests, so a real pad never takes a player */
} PsiOptions;

/* NULL if the layout is missing or invalid (no slots, more than PSI_MAX_SLOTS, a slot with no name or key). */
PsiInput *psi_new(const PsiOptions *opt);
void psi_free(PsiInput *in);

/* Every SDL event goes through here. Returns 1 if the layer used it up: a capture took it, and it should go no
   further. Device events return 0 (the port may want them too). */
int psi_event(PsiInput *in, const SDL_Event *e);

/* Key state comes from SDL's keyboard state (a game: 1) or from the events given to psi_event only (tests that push
   their own: 0, the default). */
void psi_keys_from_sdl(PsiInput *in, int on);
/* The on-screen controller's held buttons, in the port's bits, for a player (player 1's by habit). */
void psi_set_touch(PsiInput *in, int player, uint32_t held);

/* Every player's pad as it is now, once a frame. While a remap runs, nothing is pressed. */
void psi_pads(PsiInput *in, PsiPad pads[PSI_PLAYERS]);
/* The same with no SOCD cleaning, no swap and no capture blanking: what the players hold, for a settings screen's
   navigation. */
void psi_raw(PsiInput *in, PsiPad pads[PSI_PLAYERS]);

int psi_analog(const PsiInput *in, int player);
void psi_set_analog(PsiInput *in, int player, int on);      /* saved as pN.analog */
void psi_toggle_analog(PsiInput *in, int player);

/* Swaps the players' devices. */
void psi_swap_players(PsiInput *in);
/* Pins the devices with this SDL GUID text to a player (saved as pN.device) and moves the ones connected now. */
void psi_pin_device(PsiInput *in, int player, const char *guid);

/* Sets the pad-word bits a slot gives for a player: for slots the layout gave mask 0. */
void psi_set_slot_mask(PsiInput *in, int player, int slot, uint32_t mask);
/* While on, the layout's swap_a and swap_b exchange on that player's pad (psi_pads only). */
void psi_set_swap(PsiInput *in, int player, int on);

/* One slot: the next input becomes its binding of that kind for the player. Escape cancels. */
void psi_remap_begin(PsiInput *in, int player, int slot);
/* Each slot in the layout's remap order, one after another; Escape keeps a slot's bindings and goes on. */
void psi_remap_start(PsiInput *in, int player);
void psi_remap_cancel(PsiInput *in);
/* -1 while nothing is being captured, else the slot being asked for. */
int psi_remap_slot(const PsiInput *in);
/* While a run asks, a line for the screen ("Player 1: press X (now key:Z, pad:south; Esc keeps it)"); else NULL. */
const char *psi_remap_prompt(const PsiInput *in);
/* Set once by a capture or run that ended; cleared by the read. 1 taken (or the run finished), 2 cancelled. A taken
   binding is already in the store; the store's save is called then, when it has one. */
int psi_remap_take_result(PsiInput *in);
/* Back to the layout's bindings for a player (saved). */
void psi_reset_player(PsiInput *in, int player);

/* A slot's bindings as a person reads them ("Z key, south button"; "not set"). */
void psi_binding_text(const PsiInput *in, int player, int slot, char *out, size_t cap);
/* The devices a player has, as names ("DualSense Wireless Controller, keyboard"). */
void psi_player_devices(const PsiInput *in, int player, char *out, size_t cap);
/* Connected gamepads and joysticks. */
int psi_device_count(const PsiInput *in);
/* A line per connected device, to out (stderr when NULL). */
void psi_describe(const PsiInput *in, FILE *out);

/* The family of the last device used (the keyboard before any), or the forced one. */
PsiFamily psi_family(const PsiInput *in);
void psi_force_family(PsiInput *in, int family);            /* a PsiFamily, or -1 for auto; over "prompts" */
/* The player whose device was used last. */
int psi_last_player(const PsiInput *in);
/* The letter on the button or key each of n slots (n <= 8) is bound to for the player's last device: SDL's label on a
   gamepad (A, B, X, Y), the key's letter or digit on the keyboard; 0 where there is none (a PlayStation pad's
   shapes, a joystick, a trigger, a key like Enter). With a forced family, that family's labels (PlayStation and
   other: none). out gets n bytes, not terminated. */
void psi_prompt_letters(const PsiInput *in, int player, const int *slots, int n, char *out);

/* Rumble on the player's gamepads for ms: the small motor and the large one, 0 to 0xFFFF each; 0, 0 stops it. A
   no-op with "rumble = off". The port renews it while it lasts. */
void psi_rumble(PsiInput *in, int player, Uint16 small_motor, Uint16 large_motor, Uint32 ms);

/* A gamepad's family from what SDL knows of it (exported for tests). */
PsiFamily psi_gamepad_family(SDL_GamepadType type, Uint16 vendor, Uint16 product, int on_deck);
/* Running on a Steam Deck (Steam's SteamDeck=1, or the Deck's DMI name). */
int psi_on_steam_deck(void);

#ifdef __cplusplus
}
#endif

#endif
