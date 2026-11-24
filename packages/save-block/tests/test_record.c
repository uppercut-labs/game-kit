#include "sb/codec.h"
#include "sb/memory.h"
#include "sb/record.h"
#include "check.h"

static const sb_format format = { { 'T', 'E', 'S', 'T' }, 1u, 8u };

static void payload_of(uint8_t payload[8], uint8_t seed) {
    unsigned i;
    for (i = 0u; i < 8u; i++) payload[i] = (uint8_t)(seed + i);
}

static sb_store store_for(sb_memory* memory) {
    sb_store store;
    memset(&store, 0, sizeof(store));
    store.format = format;
    store.storage = sb_memory_storage(memory);
    return store;
}

static int payload_is(const uint8_t* got, uint8_t seed) {
    uint8_t want[8];
    payload_of(want, seed);
    return memcmp(got, want, 8u) == 0;
}

/* the whole record, with the checksum worked out by an independent program */
static void layout(void) {
    static const uint8_t pinned[24] = { 0x54, 0x45, 0x53, 0x54, 0x01, 0x00, 0x08, 0x00, 0x04, 0x03, 0x02, 0x01,
                                        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0xf8, 0x58, 0xc1, 0xe6 };
    uint8_t payload[8], record[24], out[8];
    uint32_t sequence = 0u;
    payload_of(payload, 0x10u);
    CHECK(sb_record_bytes(&format) == 24u);
    CHECK(sb_encode(&format, 0x01020304u, payload, record, sizeof(record)) == SB_OK);
    CHECK(memcmp(record, pinned, sizeof(pinned)) == 0);
    CHECK(sb_decode(&format, record, sizeof(record), out, &sequence) == SB_OK);
    CHECK(sequence == 0x01020304u && memcmp(out, payload, 8u) == 0);
    CHECK(sb_decode(&format, record, sizeof(record), NULL, NULL) == SB_OK);
}

static void decode_failures(void) {
    uint8_t payload[8], record[24], out[8] = { 0 };
    uint32_t sequence = 77u;
    size_t length, bit;
    payload_of(payload, 1u);
    CHECK(sb_encode(&format, 5u, payload, record, sizeof(record)) == SB_OK);

    /* every shorter length and one byte too many is damaged */
    for (length = 0u; length < sizeof(record); length++) {
        const sb_result r = sb_decode(&format, record, length, out, &sequence);
        /* a cut record that still shows the right magic and version is damaged; one cut inside them is damaged too */
        CHECK(r == SB_INVALID);
    }
    {
        uint8_t longer[25];
        memcpy(longer, record, sizeof(record));
        longer[24] = 0u;
        CHECK(sb_decode(&format, longer, sizeof(longer), out, &sequence) == SB_INVALID);
    }
    CHECK(sequence == 77u && out[0] == 0u);

    /* a flip of any single bit is never accepted */
    for (bit = 0u; bit < sizeof(record) * 8u; bit++) {
        uint8_t copy[24];
        memcpy(copy, record, sizeof(record));
        copy[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        CHECK(sb_decode(&format, copy, sizeof(copy), out, &sequence) != SB_OK);
    }
    CHECK(sequence == 77u && out[0] == 0u);

    /* the wrong magic is damaged, the wrong version is a version problem, a wrong payload size is damaged */
    {
        sb_format other = format;
        other.magic[0] = 'X';
        CHECK(sb_decode(&other, record, sizeof(record), NULL, NULL) == SB_INVALID);
        other = format;
        other.version = 2u;
        CHECK(sb_decode(&other, record, sizeof(record), NULL, NULL) == SB_UNSUPPORTED_VERSION);
        other = format;
        other.payload_bytes = 9u;
        CHECK(sb_decode(&other, record, sizeof(record), NULL, NULL) == SB_INVALID);
    }
}

static void arguments(void) {
    uint8_t payload[8], record[24];
    sb_format wide = format;
    sb_memory memory;
    sb_store store;
    payload_of(payload, 0u);
    sb_memory_init(&memory);
    store = store_for(&memory);
    CHECK(sb_encode(&format, 1u, payload, record, 23u) == SB_BAD_ARGUMENT);
    CHECK(sb_encode(&format, 1u, NULL, record, sizeof(record)) == SB_BAD_ARGUMENT);
    CHECK(sb_encode(NULL, 1u, payload, record, sizeof(record)) == SB_BAD_ARGUMENT);
    wide.payload_bytes = 0u;
    CHECK(sb_record_bytes(&wide) == 0u);
    wide.payload_bytes = SB_MAX_PAYLOAD_BYTES + 1u;
    CHECK(sb_record_bytes(&wide) == 0u);
    wide.payload_bytes = SB_MAX_PAYLOAD_BYTES;
    CHECK(sb_record_bytes(&wide) == SB_MAX_RECORD_BYTES);
    CHECK(sb_decode(&wide, record, sizeof(record), NULL, NULL) == SB_INVALID);
    wide.payload_bytes = 0u;
    CHECK(sb_decode(&wide, record, sizeof(record), NULL, NULL) == SB_BAD_ARGUMENT);
    CHECK(sb_decode(&format, NULL, 24u, NULL, NULL) == SB_BAD_ARGUMENT);
    CHECK(sb_save(NULL, payload) == SB_BAD_ARGUMENT);
    CHECK(sb_save(&store, NULL) == SB_BAD_ARGUMENT);
    CHECK(sb_load(NULL, payload, NULL) == SB_BAD_ARGUMENT);
    store.storage.write = NULL;
    CHECK(sb_save(&store, payload) == SB_BAD_ARGUMENT);
    store.storage.read = NULL;
    CHECK(sb_load(&store, payload, NULL) == SB_BAD_ARGUMENT);
    store = store_for(&memory);
    store.format.payload_bytes = 0u;
    CHECK(sb_save(&store, payload) == SB_BAD_ARGUMENT && sb_load(&store, payload, NULL) == SB_BAD_ARGUMENT);
    CHECK(memory.writes == 0u);
    /* a full-size payload round-trips */
    {
        uint8_t big[SB_MAX_PAYLOAD_BYTES], back[SB_MAX_PAYLOAD_BYTES];
        size_t i;
        for (i = 0u; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7u + 3u);
        store = store_for(&memory);
        store.format.payload_bytes = SB_MAX_PAYLOAD_BYTES;
        CHECK(sb_save(&store, big) == SB_OK);
        CHECK(sb_load(&store, back, NULL) == SB_OK && memcmp(big, back, sizeof(big)) == 0);
    }
}

static void alternating_slots(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8];
    uint32_t sequence = 0u;
    sb_memory_init(&memory);
    store = store_for(&memory);

    CHECK(sb_load(&store, loaded, &sequence) == SB_EMPTY);

    payload_of(payload, 10u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(memory.length[0] == 24u && memory.length[1] == 0u);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 10u) && sequence == 1u);

    payload_of(payload, 20u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(memory.length[1] == 24u);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 20u) && sequence == 2u);

    payload_of(payload, 30u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 30u) && sequence == 3u);
    /* the third save went into slot 0 again, and slot 1 still holds the one before it */
    CHECK(sb_decode(&format, memory.data[0], memory.length[0], loaded, &sequence) == SB_OK && payload_is(loaded, 30u));
    CHECK(sb_decode(&format, memory.data[1], memory.length[1], loaded, &sequence) == SB_OK && payload_is(loaded, 20u));

    /* a NULL payload and a NULL sequence are allowed on a load: "is there a save?" */
    CHECK(sb_load(&store, NULL, NULL) == SB_OK);
}

static void failed_writes_keep_the_last_save(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8];
    sb_memory_init(&memory);
    store = store_for(&memory);
    payload_of(payload, 1u);
    CHECK(sb_save(&store, payload) == SB_OK);
    payload_of(payload, 2u);
    CHECK(sb_save(&store, payload) == SB_OK);

    /* the power goes mid-write: the older slot is torn, the newer save is intact */
    memory.truncate_to = 9u;
    payload_of(payload, 3u);
    CHECK(sb_save(&store, payload) == SB_IO);
    memory.truncate_to = 0u;
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 2u));

    /* the medium accepts the write and returns different bytes: caught by the read-back */
    memory.corrupt_at = 14u;
    CHECK(sb_save(&store, payload) == SB_VERIFY);
    memory.corrupt_at = 0u;
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 2u));

    /* a write the device refuses */
    memory.write_error = SB_FULL;
    CHECK(sb_save(&store, payload) == SB_FULL);
    memory.write_error = SB_OK;
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 2u));

    /* and then it works again, taking the torn slot */
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 3u));
}

static void device_errors(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8];
    sb_memory_init(&memory);
    store = store_for(&memory);
    payload_of(payload, 5u);

    memory.read_error[0] = SB_NO_MEDIA;
    memory.read_error[1] = SB_NO_MEDIA;
    CHECK(sb_load(&store, loaded, NULL) == SB_NO_MEDIA);
    CHECK(sb_save(&store, payload) == SB_NO_MEDIA);
    CHECK(memory.writes == 0u);

    /* the more serious of two results wins */
    memory.read_error[0] = SB_EMPTY;
    memory.read_error[1] = SB_IO;
    CHECK(sb_load(&store, loaded, NULL) == SB_IO);
    memory.read_error[0] = SB_OK;
    memory.read_error[1] = SB_OK;

    CHECK(sb_save(&store, payload) == SB_OK);
    /* one slot unreadable: the other still loads, but a save refuses to guess */
    memory.read_error[1] = SB_IO;
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 5u));
    CHECK(sb_save(&store, payload) == SB_IO);
    CHECK(memory.writes == 1u);
    memory.read_error[1] = SB_OK;

    /* a damaged slot is only damaged: loading uses the other, saving replaces the damaged one */
    CHECK(sb_save(&store, payload) == SB_OK);
    memory.data[1][13] ^= 1u;
    CHECK(sb_load(&store, loaded, NULL) == SB_OK);
    payload_of(payload, 6u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 6u));
    CHECK(memory.writes == 3u);

    /* both damaged */
    memory.data[0][13] ^= 1u;
    memory.data[1][13] ^= 1u;
    CHECK(sb_load(&store, loaded, NULL) == SB_INVALID);
    payload_of(payload, 7u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 7u));
}

static void another_version(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8];
    sb_memory_init(&memory);
    store = store_for(&memory);
    payload_of(payload, 1u);
    CHECK(sb_save(&store, payload) == SB_OK);
    payload_of(payload, 2u);
    CHECK(sb_save(&store, payload) == SB_OK);

    /* a newer game saved into slot 0: this game can still read the other slot, but must not overwrite the newer save */
    {
        sb_format newer = format;
        newer.version = 2u;
        payload_of(payload, 9u);
        CHECK(sb_encode(&newer, 9u, payload, memory.data[0], sizeof(memory.data[0])) == SB_OK);
        memory.length[0] = sb_record_bytes(&newer);
    }
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 2u));
    payload_of(payload, 3u);
    CHECK(sb_save(&store, payload) == SB_UNSUPPORTED_VERSION);
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 2u));

    /* with only the newer game's save on the device, loading says so */
    memory.length[1] = 0u;
    CHECK(sb_load(&store, loaded, NULL) == SB_UNSUPPORTED_VERSION);
}

static void sequence_wraps(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8], record[24];
    uint32_t sequence = 0u;
    sb_memory_init(&memory);
    store = store_for(&memory);

    /* slot 1 holds UINT32_MAX; the next save goes to slot 0 as sequence 0, which is newer */
    payload_of(payload, 1u);
    CHECK(sb_encode(&format, UINT32_MAX, payload, record, sizeof(record)) == SB_OK);
    memcpy(memory.data[1], record, sizeof(record));
    memory.length[1] = sizeof(record);
    payload_of(payload, 2u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(memory.length[0] == 24u && sb_get_u32le(memory.data[0] + 8) == 0u);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 2u) && sequence == 0u);
    payload_of(payload, 3u);
    CHECK(sb_save(&store, payload) == SB_OK);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 3u) && sequence == 1u);

    /* equal sequences prefer slot 0 */
    sb_memory_init(&memory);
    payload_of(payload, 4u);
    CHECK(sb_encode(&format, 7u, payload, record, sizeof(record)) == SB_OK);
    memcpy(memory.data[1], record, sizeof(record));
    memory.length[1] = sizeof(record);
    payload_of(payload, 5u);
    CHECK(sb_encode(&format, 7u, payload, record, sizeof(record)) == SB_OK);
    memcpy(memory.data[0], record, sizeof(record));
    memory.length[0] = sizeof(record);
    CHECK(sb_load(&store, loaded, &sequence) == SB_OK && payload_is(loaded, 5u) && sequence == 7u);

    /* the half-way point of the sequence circle: 2^31 - 1 ahead is newer, 2^31 ahead is not */
    {
        static const uint32_t ahead[2] = { 0x7fffffffu, 0x80000000u };
        unsigned i;
        for (i = 0u; i < 2u; i++) {
            sb_memory_init(&memory);
            payload_of(payload, 6u);
            CHECK(sb_encode(&format, 10u, payload, record, sizeof(record)) == SB_OK);
            memcpy(memory.data[0], record, sizeof(record));
            memory.length[0] = sizeof(record);
            payload_of(payload, 8u);
            CHECK(sb_encode(&format, 10u + ahead[i], payload, record, sizeof(record)) == SB_OK);
            memcpy(memory.data[1], record, sizeof(record));
            memory.length[1] = sizeof(record);
            CHECK(sb_load(&store, loaded, &sequence) == SB_OK);
            CHECK(i == 0u ? (payload_is(loaded, 8u) && sequence == 10u + ahead[0]) : (payload_is(loaded, 6u) && sequence == 10u));
        }
    }
}

static int only_even_first_byte(void* context, const uint8_t* payload) {
    unsigned* calls = (unsigned*)context;
    (*calls)++;
    return (payload[0] & 1u) == 0u;
}

static void validation(void) {
    sb_memory memory;
    sb_store store;
    uint8_t payload[8], loaded[8];
    unsigned calls = 0u;
    sb_memory_init(&memory);
    store = store_for(&memory);
    store.validate = only_even_first_byte;
    store.validate_context = &calls;

    /* a payload the game rejects is not saved */
    payload_of(payload, 1u);
    CHECK(sb_save(&store, payload) == SB_INVALID);
    CHECK(memory.writes == 0u && calls == 1u);

    payload_of(payload, 2u);
    CHECK(sb_save(&store, payload) == SB_OK);
    payload_of(payload, 4u);
    CHECK(sb_save(&store, payload) == SB_OK);

    /* a record that checks out but fails the game's check (an old build, a bad edit) counts as damaged */
    {
        uint8_t record[24];
        payload_of(payload, 3u);
        CHECK(sb_encode(&format, 3u, payload, record, sizeof(record)) == SB_OK);
        memcpy(memory.data[0], record, sizeof(record));
    }
    CHECK(sb_load(&store, loaded, NULL) == SB_OK && payload_is(loaded, 4u));
    memory.length[1] = 0u;
    CHECK(sb_load(&store, loaded, NULL) == SB_INVALID);
}

static void result_text(void) {
    unsigned i;
    for (i = 0u; i <= SB_BAD_ARGUMENT + 1u; i++) {
        const char* text = sb_result_text((sb_result)i);
        CHECK(text != NULL && text[0] != '\0');
    }
    CHECK(sb_result_text(200u) != NULL);
    CHECK(strcmp(sb_result_text(SB_OK), "Complete") == 0);
    CHECK(SB_OK < SB_EMPTY && SB_EMPTY < SB_INVALID && SB_INVALID < SB_UNSUPPORTED_VERSION && SB_UNSUPPORTED_VERSION < SB_NO_MEDIA);
}

int main(void) {
    layout();
    decode_failures();
    arguments();
    alternating_slots();
    failed_writes_keep_the_last_save();
    device_errors();
    another_version();
    sequence_wraps();
    validation();
    result_text();
    return TEST_RESULT("record");
}
