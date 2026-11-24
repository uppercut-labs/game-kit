/* port-input-harness: SDL3 virtual controllers for testing a game port's input, with no driver and no hardware.
 *
 * Nothing here knows any game. Four parts:
 *
 *   profiles   a controller as data (a .profile file): vendor and product IDs, SDL type, layout, prompt family,
 *              and its physical controls by name
 *   devices    attach and detach a virtual controller of a profile; press, release, move a stick; rumble received
 *   maps       what a port expects each control to give, as data: a table from control names to the port's own
 *              state bits (a uint32), loaded from a .map file the port supplies
 *   scripts    scripted input: press, hold, release, stick, wait, plug, unplug, one step per frame
 *
 * pihrun.h is the check runner a port builds its test with.
 */
#ifndef PIH_H
#define PIH_H

#include <stddef.h>
#include <stdint.h>

#include <SDL3/SDL.h>

/* The version of this library. CMakeLists.txt reads it from here (PIH_VERSION); a release tag is port-input-harness@<this>. */
#define PIH_VERSION "0.1.0"

#define PIH_MAX_CONTROLS 96
#define PIH_SLOTS 4

typedef enum { PIH_BUTTON, PIH_AXIS, PIH_HAT } PihControlType;

typedef struct {
    char name[32];
    PihControlType type;
    int index;     /* a gamepad's SDL button or axis enum value; a joystick's button, axis or hat number */
    int value;     /* an axis' pressed value (negative: the other way); a hat's SDL_HAT_* bit */
    int rest;      /* an axis' value at rest */
} PihControl;

typedef struct {
    char name[32];
    char device_name[64];
    Uint16 vendor, product;
    int gamepad;                   /* SDL gives it a gamepad mapping (else it is a plain joystick) */
    SDL_JoystickType joytype;
    SDL_GamepadType gamepad_type;  /* what SDL should report for a gamepad (SDL_GAMEPAD_TYPE_UNKNOWN: not checked) */
    SDL_GamepadType gamepad_types[4]; /* every type that passes: the first listed, then later-SDL-or-older alternatives */
    int ngamepad_types;            /* how many of gamepad_types SDL knows by name (0: not checked) */
    char layout[16];               /* "gamepad" or "joystick": which of a map's tables applies */
    char family[16];               /* the prompt family: playstation, xbox, nintendo, steam, other */
    int naxes, nbuttons, nhats;    /* a joystick's; a gamepad has SDL's standard set */
    PihControl controls[PIH_MAX_CONTROLS];
    int ncontrols;
} PihProfile;

typedef struct PihProfiles PihProfiles;

/* Reads every *.profile in dir. NULL with a message in err on failure. */
PihProfiles *pih_profiles_load(const char *dir, char *err, size_t cap);
void pih_profiles_free(PihProfiles *p);
int pih_profiles_count(const PihProfiles *p);
const PihProfile *pih_profiles_get(const PihProfiles *p, int i);
const PihProfile *pih_profiles_find(const PihProfiles *p, const char *name);   /* NULL if none */
const PihControl *pih_find_control(const PihProfile *p, const char *name);

/* ------------------------------------------------------------------ devices */

typedef struct PihDevice PihDevice;

/* Plugs one in. SDL must be initialised with SDL_INIT_GAMEPAD. NULL with SDL's error if it can't be. */
PihDevice *pih_attach(const PihProfile *p);
void pih_detach(PihDevice *d);
const PihProfile *pih_profile(const PihDevice *d);
SDL_JoystickID pih_id(const PihDevice *d);
SDL_Joystick *pih_joystick(const PihDevice *d);

/* The state takes effect at the next SDL_UpdateJoysticks. */
void pih_set_control(PihDevice *d, const PihControl *c, int down);
int pih_press(PihDevice *d, const char *control, int down);   /* by name; 0 if the profile has no such control */
void pih_release_all(PihDevice *d);
/* A stick as the bytes a pad reports (0x80 centred): which 0 left, 1 right. A joystick has only its first two axes. */
void pih_stick(PihDevice *d, int which, uint8_t x, uint8_t y);
/* An axis by its raw value, for thresholds (gamepad: an SDL_GamepadAxis; joystick: its axis number). */
void pih_raw_axis(PihDevice *d, int axis, int value);

/* What the port asked of the motors: the calls received, the last strengths. */
unsigned pih_rumble_calls(const PihDevice *d);
unsigned pih_rumble_last(const PihDevice *d, int *low, int *high);

/* ------------------------------------------------------------------ maps */

/* A port's table from controls to its own state bits, from a text file:
 *
 *     # <who> <control> <bits> [primary]
 *     gamepad south 0x40 primary        who: a layout ("gamepad", "joystick") or a profile's name, which wins
 *     deck leftpaddle1 0x280
 *
 * bits is what the port reads when that control is pressed alone. primary marks the control a script presses to give
 * those bits (a d-pad rather than the stick that also steers). bits 0: the control does nothing. */
typedef struct PihMap PihMap;

PihMap *pih_map_load(const char *path, char *err, size_t cap);
void pih_map_free(PihMap *m);

typedef struct {
    const PihControl *control;
    uint32_t bits;
    int primary;
} PihMapEntry;

/* Every control of the profile with what the port expects of it. Controls the map doesn't name get bits 0 and are
   listed too (a button nobody mapped must do nothing). The array is the map's; valid until the next call. */
const PihMapEntry *pih_map_entries(PihMap *m, const PihProfile *p, int *n);
/* Holds exactly the primary controls whose bits are in want; returns the wanted bits no primary control gives. */
uint32_t pih_hold(PihDevice *d, PihMap *m, uint32_t want);

/* ------------------------------------------------------------------ scripts */

/* Scripted input, one step per frame:
 *
 *     # comment
 *     plug <slot> <profile>             slot 0 to PIH_SLOTS-1
 *     unplug <slot>
 *     press <slot> <control>            held until released
 *     release <slot> <control>
 *     hold <slot> <control> <n>         pressed for n frames, then released; runs alongside later lines
 *     stick <slot> left|right <x> <y>   bytes, 0x80 centred
 *     axis <slot> <axis> <value>        raw
 *     release-all <slot>
 *     wait <n>                          what makes time pass
 *     end
 */
typedef struct PihRig PihRig;
typedef struct PihScript PihScript;

PihRig *pih_rig_new(const PihProfiles *profiles);
void pih_rig_free(PihRig *r);            /* unplugs what is plugged in */
PihDevice *pih_rig_slot(PihRig *r, int slot);
PihDevice *pih_rig_plug(PihRig *r, int slot, const PihProfile *p);   /* replaces what is there */
void pih_rig_unplug(PihRig *r, int slot);

PihScript *pih_script_load(const char *path, char *err, size_t cap);
PihScript *pih_script_parse(const char *text, const char *name, char *err, size_t cap);
void pih_script_free(PihScript *s);
/* One frame: applies the lines due this frame. Returns 1 while there is more, 0 after the last frame (or "end"). */
int pih_script_frame(PihScript *s, PihRig *r);
uint64_t pih_script_frames(const PihScript *s);

#endif
