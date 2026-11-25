/* test_save_block: a save-block store on an NAOMI EEPROM, over a RAM chip with faults. No KOS, no hardware. */
#include <stdio.h>
#include <string.h>

#include "kcab/ram.h"
#include "kcab/save_block.h"

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

static const sb_format FORMAT = { { 'N', 'A', 'O', '1' }, 1u, 20u }; /* a 36-byte record: two fit in 84 bytes */

/* a header the BIOS would accept: the checksum of the first slot, twice, and the size, twice */
static int header_ok(const uint8_t *image, uint8_t record_bytes)
{
    uint16_t crc = kcab_crc16(image + 44, record_bytes);

    return image[36] == (crc & 255) && image[37] == (crc >> 8) && image[40] == image[36] && image[41] == image[37] &&
           image[38] == record_bytes && image[39] == record_bytes && image[42] == record_bytes && image[43] == record_bytes;
}

static void bios_bytes(KcabRam *ram)
{
    int i;

    for (i = 0; i < 36; i++) {
        ram->image[i] = (uint8_t)(0x40 + i * 3);
    }
}

static void test_fits(void)
{
    sb_format f = FORMAT;

    CHECK(kcab_sb_fits(&f), "20 bytes of payload");
    f.payload_bytes = 26;
    CHECK(kcab_sb_fits(&f) && sb_record_bytes(&f) == 42, "26 bytes is the most: a 42-byte record, twice, is 84");
    f.payload_bytes = 27;
    CHECK(!kcab_sb_fits(&f), "27 bytes is too many");
    f.payload_bytes = 0;
    CHECK(!kcab_sb_fits(&f), "0 bytes is not a format");
    f.payload_bytes = 500;
    CHECK(!kcab_sb_fits(&f), "a big format does not wrap around to a small record size");
}

static void test_round_trip(void)
{
    KcabRam ram;
    KcabEeprom e;
    KcabSbStorage ctx;
    sb_store store;
    uint8_t payload[20], out[20], bios[36];
    uint32_t sequence = 0;
    sb_result r;
    int i, round;

    kcab_ram_init(&ram, 0xFF);
    bios_bytes(&ram);
    memcpy(bios, ram.image, 36);
    e = kcab_ram_eeprom(&ram);
    memset(&store, 0, sizeof(store));
    store.format = FORMAT;
    store.storage = kcab_sb_storage(&ctx, &e, &FORMAT);

    CHECK(sb_load(&store, out, &sequence) == SB_EMPTY, "a blank chip is empty");

    /* many saves: they alternate slots, each loads back, and every one leaves a header the BIOS accepts */
    for (round = 1; round <= 8; round++) {
        for (i = 0; i < 20; i++) {
            payload[i] = (uint8_t)(round * 17 + i);
        }
        r = sb_save(&store, payload);
        CHECK(r == SB_OK, "save %d: %s", round, sb_result_text(r));
        r = sb_load(&store, out, &sequence);
        CHECK(r == SB_OK && memcmp(out, payload, 20) == 0 && sequence == (uint32_t)round, "load %d: %s seq %u", round,
              sb_result_text(r), (unsigned)sequence);
        CHECK(header_ok(ram.image, 36), "the header is valid after save %d", round);
        CHECK(memcmp(ram.image, bios, 36) == 0, "the BIOS bytes are untouched after save %d", round);
    }

    /* the two slots both hold records */
    {
        uint8_t blank[36];

        memset(blank, 0xFF, sizeof(blank));
        CHECK(memcmp(ram.image + 44, blank, 36) != 0 && memcmp(ram.image + 80, blank, 36) != 0, "both slots are in use");
    }

    /* a power cut during a save: the older slot is the one written, the last good save still loads */
    for (i = 0; i < 20; i++) {
        payload[i] = (uint8_t)(0xE0 + i);
    }
    ram.drop_write_after = 70; /* the tear lands inside the slot being written */
    r = sb_save(&store, payload);
    CHECK(r == SB_VERIFY, "a torn write is reported: %s", sb_result_text(r));
    ram.drop_write_after = 0;
    r = sb_load(&store, out, &sequence);
    CHECK(r == SB_OK && sequence == 8u, "the save before the cut loads: %s seq %u", sb_result_text(r), (unsigned)sequence);

    /* a failed write */
    ram.fail_writes = 1;
    CHECK(sb_save(&store, payload) == SB_IO, "a refused write is an I/O error");
    /* an unreadable chip */
    ram.fail_reads = 2; /* a load reads both slots */
    CHECK(sb_load(&store, out, NULL) == SB_NO_MEDIA, "an unreadable chip reads as no media");
    ram.fail_reads = 1;
    CHECK(sb_save(&store, payload) != SB_OK, "and a save to it stops");
    /* a bit flipped in what was written */
    ram.corrupt_write_byte = 90;
    CHECK(sb_save(&store, payload) == SB_VERIFY, "a flipped bit is caught by the read-back");
    ram.corrupt_write_byte = 0;
}

static void test_other_layouts(void)
{
    KcabRam ram;
    KcabEeprom e;
    KcabSbStorage ctx;
    sb_store store;
    KcabBlock mine = { (const uint8_t *)"SHP1", 4, 8, 2 };
    uint8_t block[8] = { 'S', 'H', 'P', '1', 7, 0, 0, 0 }, payload[20] = { 0 }, out[20];

    /* a chip with another game's block in it is empty to this store, and the first save replaces that block */
    kcab_ram_init(&ram, 0xFF);
    bios_bytes(&ram);
    e = kcab_ram_eeprom(&ram);
    CHECK(kcab_block_save(&e, &mine, block) == KCAB_OK, "another layout's block");
    memset(&store, 0, sizeof(store));
    store.format = FORMAT;
    store.storage = kcab_sb_storage(&ctx, &e, &FORMAT);
    CHECK(sb_load(&store, out, NULL) == SB_EMPTY, "it is empty to this store");
    CHECK(sb_save(&store, payload) == SB_OK && sb_load(&store, out, NULL) == SB_OK, "a save works over it");
    CHECK(header_ok(ram.image, 36), "with the header of this layout");

    /* a format that does not fit: every write is refused and nothing is changed */
    {
        sb_format big = FORMAT;
        uint8_t before[128];

        big.payload_bytes = 30;
        memcpy(before, ram.image, sizeof(before));
        memset(&store, 0, sizeof(store));
        store.format = big;
        store.storage = kcab_sb_storage(&ctx, &e, &big);
        CHECK(sb_save(&store, (const uint8_t[30]){ 0 }) != SB_OK && memcmp(before, ram.image, sizeof(before)) == 0,
              "a format too big for the chip is refused and writes nothing");
    }
}

int main(void)
{
    test_fits();
    test_round_trip();
    test_other_layouts();
    printf("test_save_block: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
