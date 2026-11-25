/* test_eeprom: the settings block in a 128-byte EEPROM image, and over a RAM chip with faults. No KOS, no hardware. */
#include <stdio.h>
#include <string.h>

#include "kcab/eeprom.h"
#include "kcab/ram.h"

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

static const uint8_t MAGIC[4] = { 'T', 'S', 'T', '1' };

/* a stand-in for what the BIOS leaves in bytes 0..35: two copies of a checksummed 16 bytes (synthetic) */
static void bios_bytes(uint8_t image[KCAB_EEPROM_BYTES], uint8_t seed)
{
    uint16_t crc;
    int i;

    for (i = 2; i < 18; i++) {
        image[i] = (uint8_t)(seed * 7 + i * 13);
    }
    crc = kcab_crc16(image + 2, 16);
    image[0] = image[18] = (uint8_t)(crc & 0xFFu);
    image[1] = image[19] = (uint8_t)(crc >> 8);
    memcpy(image + 20, image + 2, 16); /* bytes 20..35: another synthetic run */
    image[20] ^= 0x5A;
}

static void make_block(uint8_t *data, const KcabBlock *b, uint8_t seed)
{
    int i;

    memcpy(data, MAGIC, 4);
    for (i = 4; i < b->size; i++) {
        data[i] = (uint8_t)(seed + i * 5);
    }
}

static void test_crc(void)
{
    uint8_t ff[42];
    uint8_t zero_to_fifteen[16];
    int i;

    for (i = 0; i < 16; i++) {
        zero_to_fifteen[i] = (uint8_t)i;
    }
    memset(ff, 0xFF, sizeof(ff));
    /* pinned: computed by a second implementation of the documented algorithm and by KallistiOS's own function */
    CHECK(kcab_crc16((const uint8_t *)"", 0) == 0xAC78, "empty: %04x", kcab_crc16((const uint8_t *)"", 0));
    CHECK(kcab_crc16((const uint8_t *)"123456789", 9) == 0x5EF5, "123456789");
    CHECK(kcab_crc16((const uint8_t *)"TSTx\1\0\0\0", 8) != 0, "a header-sized block");
    CHECK(kcab_crc16((const uint8_t *)"SHP1\1\0\0\0", 8) == 0x4D09, "SHP1 block");
    CHECK(kcab_crc16(zero_to_fifteen, 16) == 0x385D, "0..15");
    CHECK(kcab_crc16(ff, sizeof(ff)) == 0x91C5, "42 x 0xFF");
}

static void test_fits(void)
{
    KcabBlock b = { MAGIC, 4, 42, 2 };

    CHECK(kcab_block_fits(&b), "42 bytes twice fits (84)");
    b.size = 43;
    CHECK(!kcab_block_fits(&b), "43 bytes twice does not");
    b.copies = 1;
    CHECK(kcab_block_fits(&b), "43 once fits");
    b.size = 84;
    CHECK(kcab_block_fits(&b), "84 once fits (to the last byte)");
    b.size = 85;
    CHECK(!kcab_block_fits(&b), "85 does not");
    b.size = 0;
    CHECK(!kcab_block_fits(&b), "0 does not");
    b.size = 8;
    b.copies = 0;
    CHECK(!kcab_block_fits(&b), "0 copies");
    b.copies = 3;
    CHECK(!kcab_block_fits(&b), "3 copies");
    b.copies = 1;
    b.magic_len = 9;
    CHECK(!kcab_block_fits(&b), "magic longer than the block");
    b.magic_len = 4;
    b.magic = NULL;
    CHECK(!kcab_block_fits(&b), "magic length with no magic");
    b.magic_len = 0;
    CHECK(kcab_block_fits(&b), "no magic is allowed (the checksum is then all there is)");
    CHECK(!kcab_block_fits(NULL), "NULL");
}

static void test_store(void)
{
    uint8_t image[KCAB_EEPROM_BYTES], before[KCAB_EEPROM_BYTES], data[84], out[84];
    KcabBlock two = { MAGIC, 4, 10, 2 };
    KcabBlock one = { MAGIC, 4, 80, 1 };
    uint16_t crc;
    int i;

    /* two copies */
    for (i = 0; i < 128; i++) {
        image[i] = (uint8_t)(0xA0 + i);
    }
    bios_bytes(image, 3);
    memcpy(before, image, sizeof(image));
    make_block(data, &two, 9);
    CHECK(kcab_image_store(image, &two, data) == KCAB_OK, "store");
    CHECK(memcmp(image, before, KCAB_SYSTEM_BYTES) == 0, "bytes 0..35 are untouched");
    CHECK(memcmp(image + 44, data, 10) == 0 && memcmp(image + 54, data, 10) == 0, "both copies are there");
    CHECK(memcmp(image + 64, before + 64, 64) == 0, "bytes after the block are untouched");
    crc = kcab_crc16(data, 10);
    CHECK(image[36] == (crc & 255) && image[37] == (crc >> 8) && image[40] == image[36] && image[41] == image[37],
          "the checksum is in the header twice, low byte first");
    CHECK(image[38] == 10 && image[39] == 10 && image[42] == 10 && image[43] == 10, "the size is in the header twice over");
    CHECK(kcab_image_fetch(image, &two, out) == KCAB_OK && memcmp(out, data, 10) == 0, "fetch gives it back");

    /* one copy, the biggest the area takes */
    for (i = 0; i < 128; i++) {
        image[i] = (uint8_t)(0x30 + i);
    }
    bios_bytes(image, 5);
    memcpy(before, image, sizeof(image));
    one.size = 84;
    make_block(data, &one, 1);
    CHECK(kcab_image_store(image, &one, data) == KCAB_OK, "store 84");
    CHECK(memcmp(image, before, KCAB_SYSTEM_BYTES) == 0 && memcmp(image + 44, data, 84) == 0, "84 bytes fill the area");
    CHECK(kcab_image_fetch(image, &one, out) == KCAB_OK && memcmp(out, data, 84) == 0, "fetch 84");

    /* refusals */
    make_block(data, &two, 2);
    data[0] = 'X';
    CHECK(kcab_image_store(image, &two, data) == KCAB_BAD_ARGUMENT, "a block that does not start with the magic is refused");
    CHECK(kcab_image_store(image, NULL, data) == KCAB_BAD_ARGUMENT && kcab_image_store(NULL, &two, data) == KCAB_BAD_ARGUMENT &&
              kcab_image_store(image, &two, NULL) == KCAB_BAD_ARGUMENT,
          "NULLs are refused");
}

/* every single-bit flip in an image holding a two-copy block */
static void test_flips_two_copies(void)
{
    KcabBlock b = { MAGIC, 4, 8, 2 };
    uint8_t good[KCAB_EEPROM_BYTES], image[KCAB_EEPROM_BYTES], data[8], out[8];
    int byte, bit, ok = 0, recovered = 0;

    memset(good, 0xFF, sizeof(good));
    bios_bytes(good, 1);
    make_block(data, &b, 4);
    kcab_image_store(good, &b, data);
    for (byte = 0; byte < 128; byte++) {
        for (bit = 0; bit < 8; bit++) {
            kcab_result r;

            memcpy(image, good, sizeof(image));
            image[byte] ^= (uint8_t)(1u << bit);
            memset(out, 0, sizeof(out));
            r = kcab_image_fetch(image, &b, out);
            if (byte < 36 || byte >= 44 + 16) {
                CHECK(r == KCAB_OK && memcmp(out, data, 8) == 0, "byte %d is not part of the block: %u", byte, r);
                ok++;
            } else {
                /* the block, its copy, or the header: the data still comes back, and the result says something was damaged */
                CHECK(r == KCAB_RECOVERED && memcmp(out, data, 8) == 0, "byte %d bit %d: %u", byte, bit, r);
                recovered++;
            }
        }
    }
    CHECK(ok == 8 * (36 + 68) && recovered == 8 * 24, "counts %d %d", ok, recovered);

    /* a flip in each copy leaves nothing to trust */
    memcpy(image, good, sizeof(image));
    image[44 + 5] ^= 1;
    image[52 + 5] ^= 2;
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_INVALID, "both copies damaged");
    /* both checksums damaged */
    memcpy(image, good, sizeof(image));
    image[36] ^= 1;
    image[40] ^= 2;
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_INVALID, "both checksums damaged");
    /* the magic wrong in both copies */
    memcpy(image, good, sizeof(image));
    image[44] = 'X';
    image[52] = 'X';
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_INVALID, "wrong magic");
    /* the sizes all wrong: not this layout */
    memcpy(image, good, sizeof(image));
    image[38] = image[39] = image[42] = image[43] = 9;
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_EMPTY, "another layout");
}

static void test_flips_one_copy(void)
{
    KcabBlock b = { MAGIC, 4, 80, 1 };
    uint8_t good[KCAB_EEPROM_BYTES], image[KCAB_EEPROM_BYTES], data[80], out[80];
    int byte, bit;

    memset(good, 0xFF, sizeof(good));
    bios_bytes(good, 2);
    make_block(data, &b, 6);
    kcab_image_store(good, &b, data);
    for (byte = 36; byte < 128; byte++) {
        for (bit = 0; bit < 8; bit++) {
            kcab_result r;

            memcpy(image, good, sizeof(image));
            image[byte] ^= (uint8_t)(1u << bit);
            r = kcab_image_fetch(image, &b, out);
            if (byte >= 124) {
                CHECK(r == KCAB_OK, "byte %d is after the block", byte);
            } else if (byte >= 44) {
                CHECK(r == KCAB_INVALID, "with one copy a flip in byte %d is not recoverable: %u", byte, r);
            } else {
                CHECK(r == KCAB_RECOVERED && memcmp(out, data, 80) == 0, "header byte %d: %u", byte, r);
            }
        }
    }
}

static void test_empty(void)
{
    KcabBlock b = { MAGIC, 4, 8, 2 };
    KcabBlock other = { MAGIC, 4, 10, 2 };
    uint8_t image[KCAB_EEPROM_BYTES], data[10], out[10];

    memset(image, 0xFF, sizeof(image));
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_EMPTY, "a blank chip");
    memset(image, 0x00, sizeof(image));
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_EMPTY, "a zeroed chip");
    memset(image, 0xFF, sizeof(image));
    make_block(data, &other, 1);
    kcab_image_store(image, &other, data);
    CHECK(kcab_image_fetch(image, &b, out) == KCAB_EMPTY, "another layout's block (other size) is empty to this one");
    CHECK(kcab_image_fetch(image, &other, out) == KCAB_OK, "and fine to its own");
}

/* The KallistiOS function mie_eeprom_fix_crc, restated from its source: the game checksum is computed only when the
   size byte 39 is 1..84, over that many bytes from offset 44, and goes to 36,37 and 40,41. It never writes 38, 39, 42 or 43. */
static void restated_fix_crc(uint8_t image[KCAB_EEPROM_BYTES])
{
    uint8_t size = image[39];
    uint16_t crc;

    if (size == 0 || 44u + size > KCAB_EEPROM_BYTES) {
        return;
    }
    crc = kcab_crc16(image + 44, size);
    image[36] = image[40] = (uint8_t)(crc & 255);
    image[37] = image[41] = (uint8_t)(crc >> 8);
}

static void test_bios_fix_crc(void)
{
    KcabBlock b = { MAGIC, 4, 80, 1 };
    uint8_t data[80], image[KCAB_EEPROM_BYTES], theirs[KCAB_EEPROM_BYTES];
    int i, sizes[3] = { 0xFF, 0x00, 0x08 };

    make_block(data, &b, 3);

    /* our image is a fixed point of it: the header we write is what fix_crc would compute */
    memset(image, 0xFF, sizeof(image));
    bios_bytes(image, 4);
    kcab_image_store(image, &b, data);
    memcpy(theirs, image, sizeof(image));
    restated_fix_crc(theirs);
    CHECK(memcmp(image, theirs, sizeof(image)) == 0, "fix_crc leaves a stored image as it is");

    /* the drift: a block copied in with no size header. A blank (0xFF) or zero size makes fix_crc skip the game
       checksum, and a stale size of 8 makes it cover 8 of the 80 bytes. */
    for (i = 0; i < 3; i++) {
        uint16_t stored, full = kcab_crc16(data, 80);

        memset(image, 0xFF, sizeof(image));
        bios_bytes(image, 4);
        image[38] = image[39] = image[42] = image[43] = (uint8_t)sizes[i];
        image[36] = image[37] = image[40] = image[41] = 0xAA;
        memcpy(image + 44, data, 80);
        restated_fix_crc(image);
        stored = (uint16_t)(image[36] | image[37] << 8);
        CHECK(stored != full, "header size %02x: fix_crc alone does not cover the 80 bytes", sizes[i]);
        CHECK((sizes[i] == 8) == (stored == kcab_crc16(data, 8)), "header size %02x: covers the first 8 bytes only when the header said 8", sizes[i]);
    }
}

static void test_over_a_chip(void)
{
    KcabBlock b = { MAGIC, 4, 12, 2 };
    KcabRam ram;
    KcabEeprom e;
    uint8_t data[12], old_data[12], out[12], bios[KCAB_EEPROM_BYTES], before[KCAB_EEPROM_BYTES];
    kcab_result r;

    memset(bios, 0xFF, sizeof(bios));
    bios_bytes(bios, 8);
    kcab_ram_init(&ram, 0xFF);
    memcpy(ram.image, bios, sizeof(bios)); /* the BIOS's bytes on an otherwise blank chip */
    e = kcab_ram_eeprom(&ram);
    make_block(data, &b, 1);

    CHECK(kcab_block_load(&e, &b, out) == KCAB_EMPTY, "load from a blank chip");
    CHECK(kcab_block_save(&e, &b, data) == KCAB_OK, "save");
    CHECK(memcmp(ram.image, bios, KCAB_SYSTEM_BYTES) == 0, "the BIOS bytes are as they were");
    CHECK(kcab_block_load(&e, &b, out) == KCAB_OK && memcmp(out, data, 12) == 0, "load what was saved");

    /* a second save replaces the first and keeps everything else on the chip as it was */
    ram.image[100] = 0x77; /* another program's byte after the block */
    memcpy(before, ram.image, sizeof(before));
    make_block(data, &b, 50);
    CHECK(kcab_block_save(&e, &b, data) == KCAB_OK, "save again");
    CHECK(ram.image[100] == 0x77 && memcmp(ram.image, bios, KCAB_SYSTEM_BYTES) == 0, "the rest is kept");
    CHECK(kcab_block_load(&e, &b, out) == KCAB_OK && memcmp(out, data, 12) == 0, "load the new one");
    CHECK(memcmp(before + 44, ram.image + 44, 24) != 0, "and the block changed");

    /* the chip cannot be read */
    ram.fail_reads = 1;
    CHECK(kcab_block_load(&e, &b, out) == KCAB_NO_EEPROM, "load, chip unreadable");
    ram.fail_reads = 1;
    memcpy(before, ram.image, sizeof(before));
    CHECK(kcab_block_save(&e, &b, data) == KCAB_NO_EEPROM && memcmp(before, ram.image, sizeof(before)) == 0 && ram.writes == 2,
          "save, chip unreadable: nothing written");

    /* the write is refused: nothing changes */
    memcpy(before, ram.image, sizeof(before));
    make_block(data, &b, 99);
    ram.fail_writes = 1;
    CHECK(kcab_block_save(&e, &b, data) == KCAB_WRITE_FAILED && memcmp(before, ram.image, sizeof(before)) == 0, "write refused");
    CHECK(kcab_block_load(&e, &b, out) == KCAB_OK && memcmp(out, data, 12) != 0, "the old save is still there");

    /* the write is accepted but a bit comes back different */
    ram.corrupt_write_byte = 50; /* byte 49, inside the block */
    CHECK(kcab_block_save(&e, &b, data) == KCAB_VERIFY_FAILED, "a flipped bit is caught by the read-back");
    ram.corrupt_write_byte = 3; /* byte 2, a BIOS byte: also caught, it is compared too */
    CHECK(kcab_block_save(&e, &b, data) == KCAB_VERIFY_FAILED, "a changed BIOS byte is caught");
    ram.corrupt_write_byte = 0;

    /* a torn write: only the first 60 bytes land. The write is caught, and the chip then holds a whole old block or a
       whole new one, never a mix. */
    kcab_ram_init(&ram, 0xFF);
    memcpy(ram.image, bios, sizeof(bios));
    make_block(old_data, &b, 8);
    CHECK(kcab_block_save(&e, &b, old_data) == KCAB_OK, "a good first save");
    make_block(data, &b, 20);
    ram.drop_write_after = 60;
    CHECK(kcab_block_save(&e, &b, data) == KCAB_VERIFY_FAILED, "a torn write is caught");
    ram.drop_write_after = 0;
    r = kcab_block_load(&e, &b, out);
    CHECK((r == KCAB_OK || r == KCAB_RECOVERED) && (memcmp(out, data, 12) == 0 || memcmp(out, old_data, 12) == 0),
          "after a torn write the load is a whole block: %u", r);
    /* a tear that stops before the block leaves the old save intact */
    kcab_ram_init(&ram, 0xFF);
    memcpy(ram.image, bios, sizeof(bios));
    kcab_block_save(&e, &b, old_data);
    ram.drop_write_after = 40;
    CHECK(kcab_block_save(&e, &b, data) == KCAB_VERIFY_FAILED, "a tear inside the header is caught");
    ram.drop_write_after = 0;
    CHECK(kcab_block_load(&e, &b, out) != KCAB_NO_EEPROM, "and the chip still reads");

    /* the read-back itself fails */
    kcab_ram_init(&ram, 0xFF);
    e = kcab_ram_eeprom(&ram);
    {
        /* first read (for the save) works; the read-back fails: fail_reads counts down per call, so arm it between */
        uint8_t image[KCAB_EEPROM_BYTES];
        memset(image, 0xFF, sizeof(image));
        image[0] = 1;
        ram.fail_reads = 1;
        CHECK(kcab_eeprom_write_verified(&e, image) == KCAB_VERIFY_FAILED, "an unreadable read-back is not a pass");
        CHECK(kcab_eeprom_write_verified(&e, image) == KCAB_OK, "write_verified on a healthy chip");
        CHECK(kcab_eeprom_write_verified(NULL, image) == KCAB_BAD_ARGUMENT && kcab_eeprom_write_verified(&e, NULL) == KCAB_BAD_ARGUMENT,
              "write_verified arguments");
    }

    /* arguments */
    CHECK(kcab_block_save(NULL, &b, data) == KCAB_BAD_ARGUMENT && kcab_block_save(&e, NULL, data) == KCAB_BAD_ARGUMENT &&
              kcab_block_save(&e, &b, NULL) == KCAB_BAD_ARGUMENT,
          "save arguments");
    CHECK(kcab_block_load(NULL, &b, out) == KCAB_BAD_ARGUMENT && kcab_block_load(&e, NULL, out) == KCAB_BAD_ARGUMENT &&
              kcab_block_load(&e, &b, NULL) == KCAB_BAD_ARGUMENT,
          "load arguments");
    data[0] = 'Z';
    CHECK(kcab_block_save(&e, &b, data) == KCAB_BAD_ARGUMENT, "save of a block with the wrong magic");
    {
        kcab_result k;

        for (k = 0; k <= KCAB_BAD_ARGUMENT + 1; k++) {
            CHECK(kcab_result_text(k) != NULL && kcab_result_text(k)[0] != '\0', "text for %u", k);
        }
    }
}

int main(void)
{
    test_crc();
    test_fits();
    test_store();
    test_flips_two_copies();
    test_flips_one_copy();
    test_empty();
    test_bios_fix_crc();
    test_over_a_chip();
    printf("test_eeprom: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
