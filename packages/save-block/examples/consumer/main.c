/*
 * A game's save code using save-block: settings and a hiscore table in two alternating slots, tested against RAM
 * storage, including a power cut in the middle of a save. A real game replaces sb_memory with callbacks over its
 * platform's file, card or EEPROM API.
 */
#include <stdio.h>
#include <string.h>

#include "sb/arcade.h"
#include "sb/memory.h"

static const sb_format format = { { 'D', 'E', 'M', 'O' }, 1u, SB_ARCADE_PAYLOAD_BYTES };
static const sb_arcade_limits limits = { 1u, 10u, 20u }; /* two display modes, volume 0..10, levels 1..20 */

#define EXPECT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

int main(void) {
    sb_memory card;
    sb_storage storage;
    sb_arcade_profile profile, loaded;
    sb_initials_entry entry;

    sb_memory_init(&card);
    storage = sb_memory_storage(&card);

    /* first start: nothing saved, so use the defaults */
    sb_arcade_defaults(&profile);
    EXPECT(sb_arcade_load(&format, &storage, &limits, &loaded) == SB_EMPTY);

    /* a run ends with a score that makes the table: type initials, then save */
    sb_initials_begin(&entry, profile.scores, 1500u, 4u);
    EXPECT(entry.active);
    sb_initials_update(&entry, profile.scores, SB_INPUT_UP);
    sb_initials_update(&entry, profile.scores, SB_INPUT_RIGHT);
    sb_initials_update(&entry, profile.scores, SB_INPUT_DOWN);
    sb_initials_update(&entry, profile.scores, SB_INPUT_CONFIRM);
    EXPECT(!entry.active && memcmp(profile.scores[0].initials, "BZA", 3u) == 0);
    EXPECT(sb_arcade_save(&format, &storage, &limits, &profile) == SB_OK);

    /* the power goes while the next save is being written: the first save is still there */
    profile.settings.music = 3u;
    card.truncate_to = 20u;
    EXPECT(sb_arcade_save(&format, &storage, &limits, &profile) == SB_IO);
    card.truncate_to = 0u;
    EXPECT(sb_arcade_load(&format, &storage, &limits, &loaded) == SB_OK);
    EXPECT(loaded.settings.music == 8u && loaded.scores[0].score == 1500u);

    /* the next try goes through */
    EXPECT(sb_arcade_save(&format, &storage, &limits, &profile) == SB_OK);
    EXPECT(sb_arcade_load(&format, &storage, &limits, &loaded) == SB_OK);
    EXPECT(loaded.settings.music == 3u && loaded.scores[0].score == 1500u);

    printf("save-block %s: the example game saved, survived a power cut and loaded (%s)\n", SB_PACKAGE_VERSION,
           sb_result_text(SB_OK));
    return 0;
}
