/* save-block: the optional arcade profile. Part of save-block 0.1.0 (MIT). */
#include "sb/arcade.h"

#include <string.h>

#include "sb/codec.h"

enum { SETTINGS_BYTES = 4u, ROW_BYTES = 8u };

void sb_arcade_defaults(sb_arcade_profile* profile) {
    memset(profile, 0, sizeof(*profile));
    profile->settings.music = 8u;
    profile->settings.effects = 8u;
}

static int row_valid(const sb_arcade_record* row, const sb_arcade_limits* limits) {
    unsigned i;
    if (row->level > limits->level_max) return 0;
    if (row->level == 0u) {
        static const char blank[SB_ARCADE_INITIALS] = { '\0', '\0', '\0' };
        return row->score == 0u && memcmp(row->initials, blank, SB_ARCADE_INITIALS) == 0;
    }
    for (i = 0u; i < SB_ARCADE_INITIALS; i++) {
        if (row->initials[i] < ' ' || row->initials[i] > '~') return 0;
    }
    return 1;
}

int sb_arcade_valid(const sb_arcade_profile* profile, const sb_arcade_limits* limits) {
    unsigned i;
    if (!profile || !limits) return 0;
    if (profile->settings.display > limits->display_max) return 0;
    if (profile->settings.music > limits->volume_max || profile->settings.effects > limits->volume_max) return 0;
    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) {
        if (!row_valid(&profile->scores[i], limits)) return 0;
    }
    return 1;
}

sb_result sb_arcade_encode(const sb_arcade_profile* profile, const sb_arcade_limits* limits, uint8_t out[SB_ARCADE_PAYLOAD_BYTES]) {
    unsigned i;
    if (!profile || !limits || !out) return SB_BAD_ARGUMENT;
    if (!sb_arcade_valid(profile, limits)) return SB_INVALID;
    memset(out, 0, SB_ARCADE_PAYLOAD_BYTES);
    out[0] = profile->settings.display;
    out[1] = profile->settings.music;
    out[2] = profile->settings.effects;
    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) {
        uint8_t* wire = out + SETTINGS_BYTES + i * ROW_BYTES;
        sb_put_u32le(wire, profile->scores[i].score);
        memcpy(wire + 4, profile->scores[i].initials, SB_ARCADE_INITIALS);
        wire[7] = profile->scores[i].level;
    }
    return SB_OK;
}

sb_result sb_arcade_decode(const uint8_t in[SB_ARCADE_PAYLOAD_BYTES], const sb_arcade_limits* limits, sb_arcade_profile* profile) {
    sb_arcade_profile decoded;
    unsigned i;
    if (!in || !limits || !profile) return SB_BAD_ARGUMENT;
    if (in[3] != 0u) return SB_INVALID;
    memset(&decoded, 0, sizeof(decoded));
    decoded.settings.display = in[0];
    decoded.settings.music = in[1];
    decoded.settings.effects = in[2];
    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) {
        const uint8_t* wire = in + SETTINGS_BYTES + i * ROW_BYTES;
        decoded.scores[i].score = sb_get_u32le(wire);
        memcpy(decoded.scores[i].initials, wire + 4, SB_ARCADE_INITIALS);
        decoded.scores[i].level = wire[7];
    }
    if (!sb_arcade_valid(&decoded, limits)) return SB_INVALID;
    *profile = decoded;
    return SB_OK;
}

static int block_valid(void* context, const uint8_t* payload) {
    sb_arcade_profile scratch;
    return sb_arcade_decode(payload, (const sb_arcade_limits*)context, &scratch) == SB_OK;
}

static sb_result arcade_store(const sb_format* format, const sb_storage* storage, const sb_arcade_limits* limits, sb_store* store) {
    if (!format || !storage || !limits || format->payload_bytes != SB_ARCADE_PAYLOAD_BYTES) return SB_BAD_ARGUMENT;
    store->format = *format;
    store->storage = *storage;
    store->validate = block_valid;
    store->validate_context = (void*)limits;
    return SB_OK;
}

sb_result sb_arcade_save(const sb_format* format, const sb_storage* storage, const sb_arcade_limits* limits, const sb_arcade_profile* profile) {
    sb_store store;
    uint8_t block[SB_ARCADE_PAYLOAD_BYTES];
    sb_result result = arcade_store(format, storage, limits, &store);
    if (result != SB_OK) return result;
    result = sb_arcade_encode(profile, limits, block);
    if (result != SB_OK) return result;
    return sb_save(&store, block);
}

sb_result sb_arcade_load(const sb_format* format, const sb_storage* storage, const sb_arcade_limits* limits, sb_arcade_profile* profile) {
    sb_store store;
    uint8_t block[SB_ARCADE_PAYLOAD_BYTES];
    sb_result result = arcade_store(format, storage, limits, &store);
    if (result != SB_OK) return result;
    if (!profile) return SB_BAD_ARGUMENT;
    result = sb_load(&store, block, NULL);
    if (result != SB_OK) return result;
    return sb_arcade_decode(block, limits, profile);
}

uint8_t sb_hiscore_rank(const sb_arcade_record table[SB_ARCADE_ENTRIES], uint32_t score, uint8_t level) {
    uint8_t i;
    if (score == 0u || level == 0u) return SB_ARCADE_ENTRIES;
    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) {
        if (score > table[i].score || (score == table[i].score && level > table[i].level)) return i;
    }
    return SB_ARCADE_ENTRIES;
}

void sb_hiscore_insert(sb_arcade_record table[SB_ARCADE_ENTRIES], uint8_t rank, const sb_arcade_record* record) {
    if (rank >= SB_ARCADE_ENTRIES) return;
    memmove(table + rank + 1, table + rank, (size_t)(SB_ARCADE_ENTRIES - rank - 1u) * sizeof(*table));
    table[rank] = *record;
}

void sb_initials_begin(sb_initials_entry* entry, const sb_arcade_record table[SB_ARCADE_ENTRIES], uint32_t score, uint8_t level) {
    memset(entry, 0, sizeof(*entry));
    entry->rank = sb_hiscore_rank(table, score, level);
    if (entry->rank >= SB_ARCADE_ENTRIES) return;
    entry->record.score = score;
    entry->record.level = level;
    memset(entry->record.initials, 'A', SB_ARCADE_INITIALS);
    entry->active = 1u;
}

void sb_initials_update(sb_initials_entry* entry, sb_arcade_record table[SB_ARCADE_ENTRIES], unsigned pressed) {
    int letter;
    char current;
    if (!entry->active) return;
    if (pressed & SB_INPUT_CANCEL) {
        entry->active = 0u;
        return;
    }
    if (pressed & SB_INPUT_CONFIRM) {
        sb_hiscore_insert(table, entry->rank, &entry->record);
        entry->active = 0u;
        return;
    }
    if ((pressed & SB_INPUT_LEFT) && entry->cursor > 0u) entry->cursor--;
    if ((pressed & SB_INPUT_RIGHT) && entry->cursor < SB_ARCADE_INITIALS - 1u) entry->cursor++;
    current = entry->record.initials[entry->cursor];
    letter = (current >= 'A' && current <= 'Z') ? current - 'A' : 0;
    if (pressed & SB_INPUT_UP) letter++;
    if (pressed & SB_INPUT_DOWN) letter--;
    entry->record.initials[entry->cursor] = (char)('A' + (letter + 26) % 26);
}
