/* Guest addresses, word reads, frame conversion and the hook's joypad answers, with no core. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "libretro.h"
#include "lgp/lgp.h"

static void sh4_window(LgpRamWindow *w)
{
    lgp_ram_window_init(w, 0x01ffffffu);
    lgp_ram_window_add_base(w, 0x0c000000u);
    lgp_ram_window_add_base(w, 0x8c000000u);
    lgp_ram_window_add_base(w, 0xac000000u);
}

static void test_ram_window(void)
{
    LgpRamWindow w;
    uint32_t off = 0;
    int i;

    sh4_window(&w);
    CHECK(lgp_ram_offset(&w, 0x8c0e0100u, &off) && off == 0x000e0100u, "cached SH-4 RAM: %x", (unsigned)off);
    CHECK(lgp_ram_offset(&w, 0xac0e0100u, &off) && off == 0x000e0100u, "uncached");
    CHECK(lgp_ram_offset(&w, 0x0c0e0100u, &off) && off == 0x000e0100u, "physical");
    CHECK(lgp_ram_offset(&w, 0x0d000010u, &off) && off == 0x01000010u, "the second 16 MiB bank");
    CHECK(lgp_ram_offset(&w, 0x00123456u, &off) && off == 0x00123456u, "an offset is taken as it is");
    CHECK(!lgp_ram_offset(&w, 0x0e000000u, &off), "outside RAM");
    CHECK(!lgp_ram_offset(&w, 0xa0000000u, &off), "the boot ROM is not RAM");
    CHECK(!lgp_ram_offset(&w, 0x02000000u, &off), "just past the offsets");
    CHECK(!lgp_ram_window_add_base(&w, 0x0c000100u), "a base with offset bits is refused");
    for (i = 0; i < 5; ++i) {
        lgp_ram_window_add_base(&w, (uint32_t)(0x10000000u * (unsigned)(i + 1)));
    }
    CHECK(w.base_count == LGP_MAX_RAM_BASES && !lgp_ram_window_add_base(&w, 0xf0000000u), "the base list is bounded");

    sh4_window(&w);
    CHECK(lgp_ram_parse_offset(&w, "0x8c0e0100", &off) && off == 0xe0100u, "hex text");
    CHECK(lgp_ram_parse_offset(&w, "  +917760", &off) && off == 917760u, "decimal, spaces and a plus");
    CHECK(lgp_ram_parse_offset(&w, "010", &off) && off == 8u, "octal, as C reads it");
    CHECK(!lgp_ram_parse_offset(&w, "+ 1", &off) && !lgp_ram_parse_offset(&w, "-1", &off) &&
              !lgp_ram_parse_offset(&w, "0x8c0e0100z", &off) && !lgp_ram_parse_offset(&w, "", &off) &&
              !lgp_ram_parse_offset(&w, "0x1ffffffff", &off) && !lgp_ram_parse_offset(&w, NULL, &off),
          "bad text is refused");

    lgp_ram_window_init(&w, 0x00ffffffu);
    CHECK(lgp_ram_offset(&w, 0x8c0e0100u, &off) && off == 0x0e0100u, "no bases: a plain mask");
}

static void test_nm_lookup(const char *scratch)
{
    char path[1200];
    uint32_t a = 0;
    FILE *f;
    snprintf(path, sizeof(path), "%s/ram_test.sym", scratch);
    f = fopen(path, "wb");
    if (!f) {
        exit(2);
    }
    fputs("         U _undefined\n8c010000 T _start\n8c4829c8 B _record_v2\r\n8c482cc8 B _record\n"
          "not a line\n8c482d00 b _record_v2_copy\n",
          f);
    fclose(f);
    CHECK(lgp_nm_lookup(path, "_record", &a) && a == 0x8c482cc8u, "a symbol: %x", (unsigned)a);
    CHECK(lgp_nm_lookup(path, "_record_v2", &a) && a == 0x8c4829c8u, "a whole name, with a CRLF line end");
    CHECK(lgp_nm_lookup(path, "_start", &a) && a == 0x8c010000u, "a text symbol");
    CHECK(!lgp_nm_lookup(path, "_undefined", &a) && !lgp_nm_lookup(path, "_rec", &a) &&
              !lgp_nm_lookup(path, "", &a),
          "undefined, partial or empty names are not found");
    snprintf(path, sizeof(path), "%s/no-such.sym", scratch);
    CHECK(!lgp_nm_lookup(path, "_record", &a), "a missing file");
}

static void test_read_words(void)
{
    uint8_t ram[19];
    struct {
        uint32_t version;
        float x;
        int32_t n;
    } rec;
    float half = -2.5f;
    uint32_t bits;
    unsigned i;

    memset(ram, 0, sizeof(ram));
    memcpy(&bits, &half, sizeof(bits));
    /* the record at offset 3 (unaligned), little endian */
    ram[3] = 2;
    for (i = 0; i < 4; ++i) {
        ram[7 + i] = (uint8_t)(bits >> (8 * i));
    }
    ram[11] = 0xff;
    ram[12] = 0xff;
    ram[13] = 0xff;
    ram[14] = 0xff;
    CHECK(lgp_read_words(ram, sizeof(ram), 3, &rec, 3, 0) && rec.version == 2 && rec.x == -2.5f && rec.n == -1,
          "an unaligned little-endian record of 32-bit fields");
    CHECK(lgp_read_words(ram, sizeof(ram), 15, &rec, 1, 0), "the last whole word");
    CHECK(!lgp_read_words(ram, sizeof(ram), 16, &rec, 1, 0) && !lgp_read_words(ram, sizeof(ram), 3, &rec, 5, 0) &&
              !lgp_read_words(ram, sizeof(ram), 0xffffffffu, &rec, 1, 0) && !lgp_read_words(NULL, 0, 0, &rec, 1, 0),
          "words past the block are refused");
    ram[0] = 0x12;
    ram[1] = 0x34;
    ram[2] = 0x56;
    ram[3] = 0x78;
    CHECK(lgp_read_words(ram, sizeof(ram), 0, &bits, 1, 1) && bits == 0x12345678u, "big endian");
}

static void test_convert(void)
{
    uint8_t rgb[2 * 2 * 3];
    uint32_t xrgb[6] = {0x00ff8040u, 0x00010203u, 0xdeadbeefu, 0x00000000u, 0x00ffffffu, 0x11111111u}; /* pitch 12 */
    uint16_t r565[2] = {0xf800u, 0x07e0u};
    uint16_t r1555[2] = {0x7c00u, 0x001fu};

    CHECK(lgp_convert_rgb24(RETRO_PIXEL_FORMAT_XRGB8888, xrgb, 2, 2, 12, rgb), "XRGB8888");
    CHECK(rgb[0] == 0xff && rgb[1] == 0x80 && rgb[2] == 0x40 && rgb[3] == 1 && rgb[4] == 2 && rgb[5] == 3,
          "the first row");
    CHECK(rgb[6] == 0 && rgb[7] == 0 && rgb[8] == 0 && rgb[9] == 0xff && rgb[11] == 0xff, "the pitch is honoured");
    CHECK(lgp_convert_rgb24(RETRO_PIXEL_FORMAT_RGB565, r565, 2, 1, 4, rgb) && rgb[0] == 0xff && rgb[1] == 0 &&
              rgb[2] == 0 && rgb[3] == 0 && rgb[4] == 0xff && rgb[5] == 0,
          "RGB565 red and green at full scale");
    CHECK(lgp_convert_rgb24(RETRO_PIXEL_FORMAT_0RGB1555, r1555, 2, 1, 4, rgb) && rgb[0] == 0xff && rgb[1] == 0 &&
              rgb[5] == 0xff,
          "0RGB1555 red and blue");
    CHECK(!lgp_convert_rgb24(7, r565, 1, 1, 2, rgb), "an unknown format");
}

static void test_joypad_state(void)
{
    uint16_t pad = (uint16_t)((1u << RETRO_DEVICE_ID_JOYPAD_START) | (1u << RETRO_DEVICE_ID_JOYPAD_R2));
    CHECK(lgp_joypad_state(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) == (int16_t)pad, "the mask");
    CHECK(lgp_joypad_state(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) == 1 &&
              lgp_joypad_state(pad, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 0,
          "single buttons");
    CHECK(lgp_joypad_state(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2) ==
                  0x7fff &&
              lgp_joypad_state(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2) ==
                  0,
          "the analog trigger");
    CHECK(lgp_joypad_state(pad, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 0 &&
              lgp_joypad_state(pad, RETRO_DEVICE_MOUSE, 0, 0) == 0 &&
              lgp_joypad_state(0xffffu, RETRO_DEVICE_JOYPAD, 0, 16) == 0,
          "nothing else");
}

int main(int argc, char **argv)
{
    test_ram_window();
    test_nm_lookup(argc > 1 ? argv[1] : ".");
    test_read_words();
    test_convert();
    test_joypad_state();
    printf("ram_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
