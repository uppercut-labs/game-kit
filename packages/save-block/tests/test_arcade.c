#include "sb/arcade.h"
#include "sb/codec.h"
#include "sb/memory.h"
#include "check.h"

static const sb_arcade_limits limits = { 2u, 10u, 30u };
static const sb_format format = { { 'A', 'R', 'C', '1' }, 1u, SB_ARCADE_PAYLOAD_BYTES };

static sb_arcade_record row(uint32_t score, uint8_t level, char a, char b, char c) {
    sb_arcade_record r;
    r.score = score;
    r.level = level;
    r.initials[0] = a;
    r.initials[1] = b;
    r.initials[2] = c;
    return r;
}

static void defaults_and_validity(void) {
    sb_arcade_profile p;
    unsigned i;
    sb_arcade_defaults(&p);
    CHECK(p.settings.display == 0u && p.settings.music == 8u && p.settings.effects == 8u);
    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) CHECK(p.scores[i].score == 0u && p.scores[i].level == 0u);
    CHECK(sb_arcade_valid(&p, &limits));
    CHECK(!sb_arcade_valid(NULL, &limits) && !sb_arcade_valid(&p, NULL));

    p.settings.display = 3u;
    CHECK(!sb_arcade_valid(&p, &limits));
    p.settings.display = 2u;
    p.settings.music = 11u;
    CHECK(!sb_arcade_valid(&p, &limits));
    p.settings.music = 10u;
    p.settings.effects = 11u;
    CHECK(!sb_arcade_valid(&p, &limits));
    p.settings.effects = 0u;
    CHECK(sb_arcade_valid(&p, &limits));

    p.scores[0] = row(100u, 31u, 'A', 'B', 'C');
    CHECK(!sb_arcade_valid(&p, &limits));
    p.scores[0] = row(100u, 30u, 'A', 'B', 'C');
    CHECK(sb_arcade_valid(&p, &limits));
    p.scores[0] = row(100u, 5u, 'A', '\n', 'C');
    CHECK(!sb_arcade_valid(&p, &limits));
    p.scores[0] = row(100u, 5u, 'A', '\x7f', 'C');
    CHECK(!sb_arcade_valid(&p, &limits));
    p.scores[0] = row(100u, 5u, ' ', '~', '0');
    CHECK(sb_arcade_valid(&p, &limits));
    /* an empty row must be empty all through */
    p.scores[0] = row(100u, 0u, '\0', '\0', '\0');
    CHECK(!sb_arcade_valid(&p, &limits));
    p.scores[0] = row(0u, 0u, 'A', '\0', '\0');
    CHECK(!sb_arcade_valid(&p, &limits));
    p.scores[0] = row(0u, 0u, '\0', '\0', '\0');
    CHECK(sb_arcade_valid(&p, &limits));
}

static void block_layout(void) {
    sb_arcade_profile p, q;
    uint8_t block[SB_ARCADE_PAYLOAD_BYTES];
    sb_arcade_defaults(&p);
    p.settings.display = 1u;
    p.settings.music = 10u;
    p.settings.effects = 0u;
    p.scores[0] = row(0x12345678u, 30u, 'D', 'E', 'V');
    p.scores[7] = row(3000u, 10u, 'X', ' ', '2');
    CHECK(sb_arcade_encode(&p, &limits, block) == SB_OK);
    {
        /* the block is a fixed layout; this pins it byte for byte */
        static const uint8_t head[12] = { 1u, 10u, 0u, 0u, 0x78u, 0x56u, 0x34u, 0x12u, 'D', 'E', 'V', 30u };
        static const uint8_t tail[8] = { 0xb8u, 0x0bu, 0u, 0u, 'X', ' ', '2', 10u };
        CHECK(memcmp(block, head, sizeof(head)) == 0);
        CHECK(memcmp(block + 4 + 7 * 8, tail, sizeof(tail)) == 0);
        CHECK(block[4 + 8] == 0u && block[4 + 15] == 0u);
    }
    sb_arcade_defaults(&q);
    CHECK(sb_arcade_decode(block, &limits, &q) == SB_OK);
    CHECK(q.settings.display == 1u && q.settings.music == 10u && q.settings.effects == 0u);
    CHECK(q.scores[0].score == 0x12345678u && q.scores[0].level == 30u && memcmp(q.scores[0].initials, "DEV", 3u) == 0);
    CHECK(q.scores[7].score == 3000u && q.scores[7].level == 10u && memcmp(q.scores[7].initials, "X 2", 3u) == 0);
    CHECK(q.scores[3].level == 0u);

    /* a block that is outside the limits or has a set reserved byte is refused and leaves the profile alone */
    block[3] = 1u;
    sb_arcade_defaults(&q);
    CHECK(sb_arcade_decode(block, &limits, &q) == SB_INVALID && q.settings.music == 8u);
    block[3] = 0u;
    block[1] = 11u;
    CHECK(sb_arcade_decode(block, &limits, &q) == SB_INVALID && q.settings.music == 8u);
    CHECK(sb_arcade_decode(NULL, &limits, &q) == SB_BAD_ARGUMENT);

    /* encoding an invalid profile writes nothing */
    memset(block, 0x55, sizeof(block));
    p.settings.display = 9u;
    CHECK(sb_arcade_encode(&p, &limits, block) == SB_INVALID && block[0] == 0x55u);
    CHECK(sb_arcade_encode(NULL, &limits, block) == SB_BAD_ARGUMENT);
}

static void table(void) {
    sb_arcade_record t[SB_ARCADE_ENTRIES];
    unsigned i;
    memset(t, 0, sizeof(t));
    CHECK(sb_hiscore_rank(t, 0u, 5u) == SB_ARCADE_ENTRIES);
    CHECK(sb_hiscore_rank(t, 100u, 0u) == SB_ARCADE_ENTRIES);
    CHECK(sb_hiscore_rank(t, 100u, 1u) == 0u);

    sb_hiscore_insert(t, 0u, &(sb_arcade_record){ 500u, 3u, { 'A', 'A', 'A' } });
    CHECK(sb_hiscore_rank(t, 600u, 1u) == 0u);
    CHECK(sb_hiscore_rank(t, 400u, 1u) == 1u);
    CHECK(sb_hiscore_rank(t, 500u, 4u) == 0u); /* equal score, higher level */
    CHECK(sb_hiscore_rank(t, 500u, 3u) == 1u); /* equal score and level: below */
    CHECK(sb_hiscore_rank(t, 500u, 2u) == 1u);

    for (i = 0u; i < SB_ARCADE_ENTRIES; i++) {
        sb_arcade_record r = { 1000u - i * 100u, 2u, { 'Z', 'Z', 'Z' } };
        t[i] = r;
    }
    CHECK(sb_hiscore_rank(t, 100u, 9u) == SB_ARCADE_ENTRIES); /* the last row is 300 */
    CHECK(sb_hiscore_rank(t, 250u, 1u) == SB_ARCADE_ENTRIES);
    CHECK(sb_hiscore_rank(t, 301u, 1u) == 7u);
    CHECK(sb_hiscore_rank(t, 1001u, 1u) == 0u);

    /* an insert moves the rows below down and drops the last */
    sb_hiscore_insert(t, 2u, &(sb_arcade_record){ 750u, 9u, { 'N', 'E', 'W' } });
    CHECK(t[0].score == 1000u && t[1].score == 900u && t[2].score == 750u && t[3].score == 800u);
    CHECK(t[7].score == 400u);
    CHECK(memcmp(t[2].initials, "NEW", 3u) == 0 && t[2].level == 9u);
    sb_hiscore_insert(t, 7u, &(sb_arcade_record){ 401u, 1u, { 'L', 'S', 'T' } });
    CHECK(t[6].score == 500u && t[7].score == 401u);
    sb_hiscore_insert(t, SB_ARCADE_ENTRIES, &(sb_arcade_record){ 1u, 1u, { 'N', 'O', 'P' } });
    CHECK(t[7].score == 401u);
}

static void initials(void) {
    sb_arcade_record t[SB_ARCADE_ENTRIES];
    sb_initials_entry e;
    memset(t, 0, sizeof(t));
    t[0] = row(500u, 4u, 'O', 'L', 'D');

    /* a score that does not make the table never opens the entry */
    sb_initials_begin(&e, t, 0u, 9u);
    CHECK(!e.active);
    sb_initials_update(&e, t, SB_INPUT_CONFIRM);
    CHECK(t[1].level == 0u);

    sb_initials_begin(&e, t, 700u, 6u);
    CHECK(e.active && e.rank == 0u && e.cursor == 0u && memcmp(e.record.initials, "AAA", 3u) == 0);
    CHECK(e.record.score == 700u && e.record.level == 6u);

    /* letters wrap both ways */
    sb_initials_update(&e, t, SB_INPUT_DOWN);
    CHECK(e.record.initials[0] == 'Z');
    sb_initials_update(&e, t, SB_INPUT_UP);
    CHECK(e.record.initials[0] == 'A');
    sb_initials_update(&e, t, SB_INPUT_UP);
    CHECK(e.record.initials[0] == 'B');
    sb_initials_update(&e, t, SB_INPUT_UP | SB_INPUT_DOWN);
    CHECK(e.record.initials[0] == 'B');

    /* the cursor stays within the three letters */
    sb_initials_update(&e, t, SB_INPUT_LEFT);
    CHECK(e.cursor == 0u);
    sb_initials_update(&e, t, SB_INPUT_RIGHT);
    sb_initials_update(&e, t, SB_INPUT_RIGHT);
    sb_initials_update(&e, t, SB_INPUT_RIGHT);
    CHECK(e.cursor == 2u);
    sb_initials_update(&e, t, SB_INPUT_UP | SB_INPUT_LEFT);
    CHECK(e.cursor == 1u && e.record.initials[1] == 'B');
    CHECK(memcmp(e.record.initials, "BBA", 3u) == 0);
    /* nothing is written until confirm */
    CHECK(t[0].score == 500u);

    sb_initials_update(&e, t, SB_INPUT_CONFIRM);
    CHECK(!e.active);
    CHECK(t[0].score == 700u && memcmp(t[0].initials, "BBA", 3u) == 0 && t[1].score == 500u && t[1].level == 4u);
    sb_initials_update(&e, t, SB_INPUT_CONFIRM); /* an ended entry ignores input */
    CHECK(t[2].level == 0u);

    /* cancel ends without writing, and wins over a confirm in the same frame */
    sb_initials_begin(&e, t, 650u, 2u);
    CHECK(e.active && e.rank == 1u);
    sb_initials_update(&e, t, SB_INPUT_CANCEL | SB_INPUT_CONFIRM);
    CHECK(!e.active && t[1].score == 500u);
}

static void stored(void) {
    sb_memory memory;
    sb_storage storage;
    sb_arcade_profile p, q;
    sb_format wrong = format;
    sb_memory_init(&memory);
    storage = sb_memory_storage(&memory);
    sb_arcade_defaults(&q);

    CHECK(sb_arcade_load(&format, &storage, &limits, &q) == SB_EMPTY);
    sb_arcade_defaults(&p);
    p.settings.display = 2u;
    p.scores[0] = row(4200u, 7u, 'T', 'O', 'P');
    CHECK(sb_arcade_save(&format, &storage, &limits, &p) == SB_OK);
    CHECK(sb_arcade_load(&format, &storage, &limits, &q) == SB_OK);
    CHECK(q.settings.display == 2u && q.scores[0].score == 4200u && memcmp(q.scores[0].initials, "TOP", 3u) == 0);

    p.scores[1] = row(100u, 1u, 'L', 'O', 'W');
    CHECK(sb_arcade_save(&format, &storage, &limits, &p) == SB_OK);
    CHECK(memory.length[0] == 84u && memory.length[1] == 84u);
    CHECK(sb_arcade_load(&format, &storage, &limits, &q) == SB_OK && q.scores[1].score == 100u);

    /* a profile outside the limits is not saved; the previous save stays */
    p.settings.music = 99u;
    CHECK(sb_arcade_save(&format, &storage, &limits, &p) == SB_INVALID);
    CHECK(memory.writes == 2u);
    CHECK(sb_arcade_load(&format, &storage, &limits, &q) == SB_OK && q.settings.music == 8u);

    /* a game with tighter limits treats the saved profile as damaged */
    {
        const sb_arcade_limits tight = { 1u, 10u, 30u };
        CHECK(sb_arcade_load(&format, &storage, &tight, &q) == SB_INVALID);
    }
    wrong.payload_bytes = 10u;
    CHECK(sb_arcade_save(&wrong, &storage, &limits, &p) == SB_BAD_ARGUMENT);
    CHECK(sb_arcade_load(&wrong, &storage, &limits, &q) == SB_BAD_ARGUMENT);
    CHECK(sb_arcade_load(&format, &storage, &limits, NULL) == SB_BAD_ARGUMENT);
    CHECK(sb_arcade_load(&format, NULL, &limits, &q) == SB_BAD_ARGUMENT);
}

/* the profile as the front of a bigger payload, the way a game with extra fields uses it */
static void front_of_a_larger_payload(void) {
    static const sb_format big = { { 'B', 'I', 'G', '1' }, 1u, SB_ARCADE_PAYLOAD_BYTES + 6u };
    sb_memory memory;
    sb_store store;
    uint8_t payload[SB_ARCADE_PAYLOAD_BYTES + 6u], back[SB_ARCADE_PAYLOAD_BYTES + 6u];
    sb_arcade_profile p, q;
    sb_writer w;
    sb_reader r;
    uint32_t best_time = 0u;
    uint16_t medals = 0u;
    sb_memory_init(&memory);
    memset(&store, 0, sizeof(store));
    store.format = big;
    store.storage = sb_memory_storage(&memory);

    sb_arcade_defaults(&p);
    p.scores[0] = row(9u, 2u, 'A', 'B', 'C');
    CHECK(sb_arcade_encode(&p, &limits, payload) == SB_OK);
    sb_writer_init(&w, payload + SB_ARCADE_PAYLOAD_BYTES, 6u);
    sb_writer_u32le(&w, 123456u);
    sb_writer_u16le(&w, 0x0102u);
    CHECK(sb_writer_ok(&w) && w.length == 6u);
    CHECK(sb_save(&store, payload) == SB_OK);

    CHECK(sb_load(&store, back, NULL) == SB_OK);
    sb_arcade_defaults(&q);
    CHECK(sb_arcade_decode(back, &limits, &q) == SB_OK && q.scores[0].score == 9u);
    sb_reader_init(&r, back + SB_ARCADE_PAYLOAD_BYTES, 6u);
    best_time = sb_reader_u32le(&r);
    medals = sb_reader_u16le(&r);
    CHECK(sb_reader_ok(&r) && best_time == 123456u && medals == 0x0102u);
}

int main(void) {
    defaults_and_validity();
    block_layout();
    table();
    initials();
    stored();
    front_of_a_larger_payload();
    return TEST_RESULT("arcade");
}
