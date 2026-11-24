/* save-block: an optional arcade profile: settings, a hiscore table and three-letter initials. Part of save-block 0.1.0 (MIT). */
#ifndef SB_ARCADE_H
#define SB_ARCADE_H

#include <stddef.h>
#include <stdint.h>

#include "sb/record.h"

/*
 * What most arcade-style games keep between runs: a few settings, a table of the best scores with the player's
 * initials, and the screen where a new score gets its initials typed in. This module is separate from the record
 * code and optional: a game with other needs uses sb_store alone, or puts this profile at the front of a larger
 * payload (sb_arcade_encode and sb_arcade_decode work on a 68-byte block that can be the first part of one).
 */

#define SB_ARCADE_ENTRIES 8u /* rows in the hiscore table */
#define SB_ARCADE_INITIALS 3u
#define SB_ARCADE_PAYLOAD_BYTES 68u /* 3 settings, 1 reserved, then 8 entries of 8 bytes */

/* One row of the table. Level 0 marks an empty row, and an empty row has score 0 and initials all NUL. */
typedef struct sb_arcade_record {
    uint32_t score;
    uint8_t level; /* how far the run got; also breaks a tie in score */
    char initials[SB_ARCADE_INITIALS]; /* printable ASCII, ' ' to '~', not NUL-terminated */
} sb_arcade_record;

/* Three small settings the game defines the meaning of: a display mode (0 .. display_max), two volumes (0 .. volume_max). */
typedef struct sb_arcade_settings {
    uint8_t display;
    uint8_t music;
    uint8_t effects;
} sb_arcade_settings;

typedef struct sb_arcade_profile {
    sb_arcade_settings settings;
    sb_arcade_record scores[SB_ARCADE_ENTRIES]; /* best first */
} sb_arcade_profile;

/* What counts as a valid profile for this game. A loaded profile outside these limits is treated as damaged. */
typedef struct sb_arcade_limits {
    uint8_t display_max; /* the highest display mode, inclusive */
    uint8_t volume_max; /* the highest volume, inclusive */
    uint8_t level_max; /* the highest level a score can record, inclusive */
} sb_arcade_limits;

/*
 * Display mode 0, both volumes 8, an empty table. 8 is a default and is not checked against the limits here:
 * assign your own values after the call if your volume_max is below 8.
 */
void sb_arcade_defaults(sb_arcade_profile* profile);

/* Non-zero when every setting and row is within the limits and every empty row is truly empty. */
int sb_arcade_valid(const sb_arcade_profile* profile, const sb_arcade_limits* limits);

/*
 * The 68-byte block:
 *   0 display, 1 music, 2 effects, 3 reserved (zero),
 *   4 + 8 * row: score u32 little-endian, initials 3 bytes, level 1 byte.
 * Encode returns SB_INVALID for a profile outside the limits and writes nothing. Decode returns SB_INVALID for a
 * block that is out of the limits or has a non-zero reserved byte, and leaves the profile untouched.
 */
sb_result sb_arcade_encode(const sb_arcade_profile* profile, const sb_arcade_limits* limits, uint8_t out[SB_ARCADE_PAYLOAD_BYTES]);
sb_result sb_arcade_decode(const uint8_t in[SB_ARCADE_PAYLOAD_BYTES], const sb_arcade_limits* limits, sb_arcade_profile* profile);

/* The two calls above plus an sb_store: a whole profile to and from two alternating slots. format.payload_bytes must be 68. */
sb_result sb_arcade_save(const sb_format* format, const sb_storage* storage, const sb_arcade_limits* limits, const sb_arcade_profile* profile);
sb_result sb_arcade_load(const sb_format* format, const sb_storage* storage, const sb_arcade_limits* limits, sb_arcade_profile* profile);

/*
 * The hiscore table. A score enters at the first row it beats: a higher score, or an equal score with a higher level.
 * A score of 0 or a level of 0 never enters. These work on any array of SB_ARCADE_ENTRIES rows.
 */

/* The row a score would take (0 is best), or SB_ARCADE_ENTRIES if it does not make the table. */
uint8_t sb_hiscore_rank(const sb_arcade_record table[SB_ARCADE_ENTRIES], uint32_t score, uint8_t level);

/* Puts `record` at `rank`, moving the rows below down one and dropping the last. A rank past the table does nothing. */
void sb_hiscore_insert(sb_arcade_record table[SB_ARCADE_ENTRIES], uint8_t rank, const sb_arcade_record* record);

/*
 * The initials screen as a state machine with no drawing or input code in it. Start it with the finished run's score
 * and level; if it does not make the table, `active` stays 0. While active, feed it one set of button presses per
 * frame. The letter under the cursor changes A to Z and wraps; confirm writes the record into the table and ends
 * the entry; cancel ends it without writing.
 */
enum {
    SB_INPUT_LEFT = 1u,
    SB_INPUT_RIGHT = 2u,
    SB_INPUT_UP = 4u, /* next letter */
    SB_INPUT_DOWN = 8u, /* previous letter */
    SB_INPUT_CONFIRM = 16u,
    SB_INPUT_CANCEL = 32u
};

typedef struct sb_initials_entry {
    sb_arcade_record record; /* the row being typed; initials start as "AAA" */
    uint8_t rank; /* the table row it will take, or SB_ARCADE_ENTRIES */
    uint8_t cursor; /* 0 .. 2 */
    uint8_t active; /* 1 while the entry is open */
} sb_initials_entry;

void sb_initials_begin(sb_initials_entry* entry, const sb_arcade_record table[SB_ARCADE_ENTRIES], uint32_t score, uint8_t level);
void sb_initials_update(sb_initials_entry* entry, sb_arcade_record table[SB_ARCADE_ENTRIES], unsigned pressed);

#endif
