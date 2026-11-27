/* pad-replay: a plain-text replay of controller input for a game port, a reader that is strict about it, and a recorder
 * that writes the same format. Nothing here knows a console or a game: the button bits mean what the caller says.
 * See docs/format.md for the file format and README.md for the use.
 *
 * C11, no dependencies beyond the C library. Not thread safe: one object belongs to one thread at a time.
 */
#ifndef PADR_PADR_H
#define PADR_PADR_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PADR_VERSION "1.0.0"

/* Limits. A slot is a physical pad position (a port, a multitap leg); a replay file names slots by number. */
#define PADR_MAX_SLOTS 255u
#define PADR_DEFAULT_SLOTS 8u
#define PADR_DEFAULT_MAX_EVENTS 32768u
#define PADR_MAX_LINE 255u /* longest accepted line, not counting the line break; a longer one is an error */
#define PADR_NEUTRAL_AXIS 128u

/* What a slot reports. Same numbers a console pad poll usually gives: absent, a fresh sample, or "could not read it
 * this time" (the recorder keeps the last state for that one and writes nothing). */
enum { PADR_ABSENT = 0, PADR_FRESH = 1, PADR_UNREADABLE = 2 };

/* One pad at one moment. `buttons` is the caller's own bit set (16 bits); the stick runs 0..255 with 128 at rest. */
typedef struct PadrSample {
    uint16_t buttons;
    uint8_t lx, ly;
    uint8_t connected; /* PADR_ABSENT, PADR_FRESH or PADR_UNREADABLE */
} PadrSample;

/* The sample of a pad that is not there, or at rest: no buttons, sticks at 128. */
PadrSample padr_neutral(uint8_t connected);

/* ---- reading --------------------------------------------------------------------------------------------------- */

typedef enum PadrStatus {
    PADR_OK = 0,
    PADR_ERR_OPEN,      /* the file could not be opened */
    PADR_ERR_READ,      /* a read error part way through */
    PADR_ERR_NUL,       /* a NUL byte in the file */
    PADR_ERR_LINE_LONG, /* a line longer than PADR_MAX_LINE */
    PADR_ERR_FIELD,     /* a field missing, not a plain number, or out of range */
    PADR_ERR_TRAILING,  /* something after the sixth field */
    PADR_ERR_ORDER,     /* a frame lower than the one before it */
    PADR_ERR_TOO_MANY,  /* more events than PadrOptions.max_events */
    PADR_ERR_EMPTY,     /* no event at all */
    PADR_ERR_MEMORY,    /* out of memory */
    PADR_ERR_ARGUMENT   /* a bad option (slots of 0 or over PADR_MAX_SLOTS) or a NULL where a value is needed */
} PadrStatus;

/* Where and why a load stopped. `line` is the 1-based line of the problem, 0 when it is not about one line. */
typedef struct PadrError {
    PadrStatus status;
    unsigned line;
} PadrError;

/* A short fixed name for a status ("invalid field", "too many events"), never NULL. */
const char *padr_status_text(PadrStatus status);

/* Both zero is fine: slots defaults to PADR_DEFAULT_SLOTS (8), max_events to PADR_DEFAULT_MAX_EVENTS (32768).
 * `slots` is how many slot numbers the file may use (0..slots-1). `max_events` is the most events a file may hold;
 * memory grows with the events read, not with this number. */
typedef struct PadrOptions {
    unsigned slots;
    unsigned max_events;
} PadrOptions;

typedef struct PadrScript PadrScript;

/* Read a replay file. Strict: the first bad line fails the whole load and gives NULL (no partial replay).
 * `opt` and `err` may be NULL. */
PadrScript *padr_load_file(const char *path, const PadrOptions *opt, PadrError *err);

/* Read a replay from memory (a file a platform without a file system embeds, a string a test builds). `length` bytes
 * are read, so a NUL byte inside them is an error, as in a file. */
PadrScript *padr_load_text(const char *text, size_t length, const PadrOptions *opt, PadrError *err);

void padr_free(PadrScript *script); /* NULL is fine */

unsigned padr_slots(const PadrScript *script);
unsigned padr_event_count(const PadrScript *script);
unsigned padr_last_frame(const PadrScript *script); /* the frame of the last event */

/* Fill out[0..out_slots) with every slot's state at `frame`: the last event at or before it for that slot, or neutral
 * and absent before a slot's first event. Call with frames that do not go down; a lower frame than the one before keeps
 * the state already reached (use padr_rewind to start again). A slot the file never mentions, and slots past the
 * script's own, come back absent. With a NULL script every slot is absent, so a game can call this unconditionally. */
void padr_sample(PadrScript *script, unsigned frame, PadrSample *out, unsigned out_slots);

void padr_rewind(PadrScript *script);

/* ---- writing a line -------------------------------------------------------------------------------------------- */

/* The one line a file holds for an event, with a line break, into buf. Returns the length without the NUL, or 0 when
 * buf (of `cap` bytes) is too small. An absent sample is written as `frame slot 0000 128 128 0`. */
size_t padr_format_event(char *buf, size_t cap, unsigned frame, unsigned slot, const PadrSample *sample);

/* ---- recording ------------------------------------------------------------------------------------------------- */

typedef struct PadrTrace PadrTrace;

typedef struct PadrTraceOptions {
    unsigned flush_every; /* flush the file on frames divisible by this; 0 means 60. Use a large number to rarely flush. */
} PadrTraceOptions;

/* Create a trace file. Exclusive: an existing file is never overwritten (NULL, errno EEXIST). `slots` is how many slots
 * will be sampled (1..PADR_MAX_SLOTS). `opt` may be NULL. */
PadrTrace *padr_trace_open(const char *path, unsigned slots, const PadrTraceOptions *opt);

/* Record the physical samples of a frame (before any replay replaces them). Writes a line only for a slot whose state
 * changed since the last line, or that is seen for the first time, so a pad left alone costs nothing however long the
 * session. PADR_UNREADABLE keeps the earlier state and writes nothing. A sample that is not fresh is recorded as an
 * absent pad. Returns 1, or 0 after a write error or a `count` over the slots given to padr_trace_open (errno set). */
int padr_trace_sample(PadrTrace *trace, unsigned frame, const PadrSample *samples, unsigned count);

unsigned padr_trace_records(const PadrTrace *trace); /* lines written so far */

/* Close and free. Returns 1 when every write and the close succeeded. NULL is fine (returns 1). */
int padr_trace_close(PadrTrace *trace);

/* ---- button names ---------------------------------------------------------------------------------------------- */

/* The caller's names for the bits of PadrSample.buttons, for capture scripts and logs ("Left+Cross"). The package
 * ships no table of its own: the bits are the port's. */
typedef struct PadrButton {
    const char *name;
    uint16_t mask;
} PadrButton;

/* Parse "Circle", "Left+Cross" or "none" (case does not matter) into a mask. Returns 1, or 0 for an unknown name. */
int padr_buttons_parse(const PadrButton *table, size_t count, const char *text, uint16_t *mask);

/* Write "Left+Cross" (names in table order, "none" for 0). Bits that no entry names are written as "0x" and hex.
 * Returns the length without the NUL, or 0 when buf is too small. */
size_t padr_buttons_format(const PadrButton *table, size_t count, uint16_t mask, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
