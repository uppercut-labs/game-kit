/* minimal-png test: encodes images and reads every byte back with a small decoder that understands only the stored
 * blocks this package writes, checking the signature, chunk lengths and CRCs, the zlib header and Adler-32.
 *
 *   mpng-test <scratch dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpng/mpng.h"

static int failures;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                     \
            failures++;                                                                \
        }                                                                              \
    } while (0)

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

typedef struct {
    uint32_t w, h;
    int colour_type;
    uint8_t *raw;   /* the scanlines, filter bytes included */
    size_t raw_len;
} Decoded;

/* 1 when the file is a well-formed PNG of the shape this package writes; fills d (raw is malloc'd). */
static int decode(const uint8_t *png, size_t len, Decoded *d)
{
    size_t pos = 8, zlen = 0;
    const uint8_t *z = NULL;
    int seen_ihdr = 0, seen_iend = 0, seen_idat = 0;
    uint32_t a = 1, b = 0;
    size_t i, zp;

    memset(d, 0, sizeof(*d));
    if (len < 8 || memcmp(png, "\x89PNG\r\n\x1a\n", 8) != 0) {
        return 0;
    }
    while (pos + 12 <= len && !seen_iend) {
        uint32_t n = be32(png + pos);
        const uint8_t *type = png + pos + 4, *data = png + pos + 8;

        if (pos + 12 + (size_t)n > len || be32(data + n) != mpng_crc32(0, type, 4 + (size_t)n)) {
            return 0;
        }
        if (!memcmp(type, "IHDR", 4)) {
            if (seen_ihdr || n != 13 || data[8] != 8 || data[10] || data[11] || data[12]) {
                return 0;
            }
            d->w = be32(data);
            d->h = be32(data + 4);
            d->colour_type = data[9];
            seen_ihdr = 1;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!seen_ihdr || seen_idat) { /* this package writes exactly one */
                return 0;
            }
            z = data;
            zlen = n;
            seen_idat = 1;
        } else if (!memcmp(type, "IEND", 4)) {
            seen_iend = n == 0;
            if (!seen_iend) {
                return 0;
            }
        } else {
            return 0;
        }
        pos += 12 + (size_t)n;
    }
    if (!seen_iend || pos != len || !seen_idat || zlen < 7 || z[0] != 0x78 || z[1] != 0x01 || ((z[0] << 8 | z[1]) % 31)) {
        return 0;
    }
    d->raw = malloc(zlen);
    zp = 2;
    for (;;) {
        uint32_t n, nn;
        int final;

        if (zp + 5 > zlen) {
            return 0;
        }
        final = z[zp] & 1;
        if ((z[zp] & 6) != 0) { /* only type 0 (stored) */
            return 0;
        }
        n = z[zp + 1] | (uint32_t)z[zp + 2] << 8;
        nn = z[zp + 3] | (uint32_t)z[zp + 4] << 8;
        if ((n ^ 0xFFFFu) != nn || zp + 5 + n + 4 > zlen) {
            return 0;
        }
        memcpy(d->raw + d->raw_len, z + zp + 5, n);
        d->raw_len += n;
        zp += 5 + n;
        if (final) {
            break;
        }
    }
    if (zp + 4 != zlen) {
        return 0;
    }
    for (i = 0; i < d->raw_len; i++) {
        a = (a + d->raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return be32(z + zp) == (b << 16 | a);
}

static void fill(uint8_t *px, size_t n, unsigned seed)
{
    size_t i;

    for (i = 0; i < n; i++) {
        seed = seed * 1664525u + 1013904223u;
        px[i] = (uint8_t)(seed >> 24);
    }
}

/* encode w x h from layout `in` to layout `out`, decode, and compare with what the pixels should have become */
static void round_trip(uint32_t w, uint32_t h, MpngLayout in, MpngLayout out, size_t pad)
{
    size_t bpp_in = in, bpp_out = out, stride = w * bpp_in + pad, x, y;
    uint8_t *px = malloc(stride * h), *png = NULL;
    size_t len = 0;
    Decoded d;
    MpngImage img;
    int ok;

    fill(px, stride * h, w * 31 + h);
    img.pixels = px;
    img.width = w;
    img.height = h;
    img.stride = pad ? stride : 0;
    img.layout = in;
    CHECK(mpng_encode(&img, out, &png, &len) == MPNG_OK);
    if (!png) {
        free(px);
        return;
    }
    CHECK(len == mpng_encoded_size(w, h, out));
    ok = decode(png, len, &d);
    CHECK(ok);
    if (ok) {
        CHECK(d.w == w && d.h == h);
        CHECK(d.colour_type == (out == MPNG_RGB ? 2 : 6));
        CHECK(d.raw_len == (w * bpp_out + 1) * (size_t)h);
        for (y = 0; y < h && ok; y++) {
            const uint8_t *row = d.raw + y * (w * bpp_out + 1);

            CHECK(row[0] == 0);
            for (x = 0; x < w; x++) {
                const uint8_t *src = px + y * stride + x * bpp_in, *got = row + 1 + x * bpp_out;
                int same = got[0] == src[0] && got[1] == src[1] && got[2] == src[2];

                if (out == MPNG_RGBA) {
                    same = same && got[3] == (in == MPNG_RGBA ? src[3] : 0xFF);
                }
                if (!same) {
                    printf("FAIL pixel %zu,%zu of %ux%u (%d to %d)\n", x, y, w, h, (int)in, (int)out);
                    failures++;
                    ok = 0;
                    break;
                }
            }
        }
    }
    free(d.raw);
    mpng_free(png);
    free(px);
}

static void test_crc(void)
{
    static const uint8_t digits[] = "123456789";

    CHECK(mpng_crc32(0, digits, 9) == 0xCBF43926u);          /* the standard check value */
    CHECK(mpng_crc32(0, digits, 0) == 0);
    CHECK(mpng_crc32(mpng_crc32(0, digits, 4), digits + 4, 5) == 0xCBF43926u); /* chunked */
}

/* made by an independent writer of stored blocks, so a change that reads the same either way still fails here */
static void test_golden(void)
{
    static const uint8_t rgba[] = { 255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 1, 2, 3, 4 };
    static const uint8_t golden[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xB6, 0x0D, 0x24, 0x00, 0x00, 0x00, 0x1D, 0x49,
        0x44, 0x41, 0x54, 0x78, 0x01, 0x01, 0x12, 0x00, 0xED, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x80,
        0x00, 0x00, 0x00, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x35, 0xF5, 0x04, 0x87, 0x02, 0x12, 0xDE, 0x3A, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    MpngImage img = { rgba, 2, 2, 0, MPNG_RGBA };
    uint8_t *png = NULL;
    size_t len = 0;

    CHECK(mpng_encode(&img, MPNG_RGBA, &png, &len) == MPNG_OK);
    CHECK(len == sizeof(golden) && png && memcmp(png, golden, sizeof(golden)) == 0);
    mpng_free(png);
}

static void test_shapes(void)
{
    static const uint32_t sizes[][2] = { { 1, 1 }, { 2, 1 }, { 1, 2 }, { 7, 5 }, { 64, 64 }, { 255, 3 }, { 300, 300 } };
    size_t i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        round_trip(sizes[i][0], sizes[i][1], MPNG_RGBA, MPNG_RGBA, 0);
        round_trip(sizes[i][0], sizes[i][1], MPNG_RGBA, MPNG_RGB, 0);
        round_trip(sizes[i][0], sizes[i][1], MPNG_RGB, MPNG_RGB, 0);
        round_trip(sizes[i][0], sizes[i][1], MPNG_RGB, MPNG_RGBA, 0);
    }
    /* rows with room after each one */
    round_trip(13, 9, MPNG_RGBA, MPNG_RGB, 5);
    round_trip(13, 9, MPNG_RGB, MPNG_RGB, 7);
}

/* the stored-block boundary: a stream of exactly 65535 bytes, one more, and whole blocks */
static void test_block_edges(void)
{
    round_trip(21844, 1, MPNG_RGB, MPNG_RGB, 0);   /* 65533 raw bytes: inside one block */
    round_trip(1, 13107, MPNG_RGBA, MPNG_RGBA, 0); /* 5 * 13107 = 65535: exactly one full block */
    round_trip(1, 13108, MPNG_RGBA, MPNG_RGBA, 0); /* 65540: a second block of 5 bytes */
    round_trip(16384, 1, MPNG_RGBA, MPNG_RGBA, 0); /* 65537: a row that crosses the boundary */
    round_trip(2, 21845, MPNG_RGBA, MPNG_RGBA, 0); /* 9 * 21845 = 196605 = 3 * 65535 exactly */
    round_trip(2, 21846, MPNG_RGBA, MPNG_RGBA, 0); /* one row over three full blocks */
    round_trip(1, 32767, MPNG_RGB, MPNG_RGB, 0);   /* 4 * 32767 = 131068 */
    round_trip(1, 32768, MPNG_RGB, MPNG_RGB, 0);   /* 131072 */
}

static void test_refusals(void)
{
    static const uint8_t px[16] = { 0 };
    MpngImage img = { px, 2, 2, 0, MPNG_RGBA };
    uint8_t *png = (uint8_t *)1;
    size_t len = 7;

    CHECK(mpng_encode(NULL, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    CHECK(mpng_encode(&img, MPNG_RGB, NULL, &len) == MPNG_ERR_ARGS);
    CHECK(mpng_encode(&img, MPNG_RGB, &png, NULL) == MPNG_ERR_ARGS);
    CHECK(mpng_encode(&img, (MpngLayout)5, &png, &len) == MPNG_ERR_ARGS);
    img.layout = (MpngLayout)1;
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.layout = MPNG_RGBA;
    img.width = 0;
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.width = 2;
    img.height = 0;
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.height = 2;
    img.stride = 7; /* shorter than a row of 2 RGBA pixels */
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.stride = 0;
    img.pixels = NULL;
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.pixels = px;
    CHECK(png == (uint8_t *)1 && len == 7); /* a refusal leaves the outputs alone */

    /* too big: refused before any pixel is read or memory is taken */
    img.width = 0x7FFFFFFFu;
    img.height = 0x7FFFFFFFu;
    CHECK(mpng_encode(&img, MPNG_RGBA, &png, &len) == MPNG_ERR_SIZE);
    img.width = 0x80000000u; /* over what a PNG header allows */
    img.height = 1;
    CHECK(mpng_encode(&img, MPNG_RGB, &png, &len) == MPNG_ERR_ARGS);
    img.width = 0x20000000u; /* one IDAT chunk would pass 2^31 - 1 bytes */
    img.height = 2;
    CHECK(mpng_encode(&img, MPNG_RGBA, &png, &len) == MPNG_ERR_SIZE);
    CHECK(mpng_encoded_size(0, 1, MPNG_RGB) == 0);
    CHECK(mpng_encoded_size(1, 1, (MpngLayout)9) == 0);
    CHECK(mpng_encoded_size(0x7FFFFFFFu, 0x7FFFFFFFu, MPNG_RGBA) == 0);
    /* 1 x 1 RGB: signature 8, IHDR 25, IDAT 12 + (2 + 5 + 4 + 4 Adler... ) */
    CHECK(mpng_encoded_size(1, 1, MPNG_RGB) == 8 + 25 + (12 + (2 + 5 + 4 + 4)) + 12);
    CHECK(mpng_write_stream(NULL, &img, MPNG_RGB) == MPNG_ERR_ARGS);
    CHECK(mpng_write_file(NULL, &img, MPNG_RGB) == MPNG_ERR_ARGS);
    CHECK(mpng_write_file("", &img, MPNG_RGB) == MPNG_ERR_ARGS);
    CHECK(mpng_error_text(MPNG_OK)[0] != 0 && mpng_error_text(-99)[0] != 0);
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    *len = fread(buf, 1, (size_t)n, f);
    fclose(f);
    return buf;
}

static void test_files(const char *dir)
{
    char path[1024];
    uint8_t px[12 * 7 * 4], *file;
    MpngImage img = { px, 12, 7, 0, MPNG_RGBA };
    size_t len = 0, enc_len = 0;
    uint8_t *enc = NULL;
    Decoded d;
    FILE *f;

    fill(px, sizeof(px), 9);
    snprintf(path, sizeof(path), "%s/frame.png", dir);
    remove(path);
    CHECK(mpng_write_file(path, &img, MPNG_RGB) == MPNG_OK);
    file = slurp(path, &len);
    CHECK(file != NULL);
    if (file) {
        CHECK(decode(file, len, &d));
        CHECK(d.w == 12 && d.h == 7 && d.colour_type == 2);
        free(d.raw);
        CHECK(mpng_encode(&img, MPNG_RGB, &enc, &enc_len) == MPNG_OK && enc_len == len && !memcmp(enc, file, len));
        mpng_free(enc);
        free(file);
    }

    /* a stream the caller opened stays open */
    snprintf(path, sizeof(path), "%s/stream.png", dir);
    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        CHECK(mpng_write_stream(f, &img, MPNG_RGBA) == MPNG_OK);
        CHECK(fputc('X', f) == 'X'); /* still open: a byte after the PNG */
        CHECK(fclose(f) == 0);
        file = slurp(path, &len);
        CHECK(file && len > 1 && file[len - 1] == 'X' && decode(file, len - 1, &d));
        free(d.raw);
        free(file);
    }

    /* a path that cannot be created */
    snprintf(path, sizeof(path), "%s/no-such-folder/x.png", dir);
    CHECK(mpng_write_file(path, &img, MPNG_RGB) == MPNG_ERR_IO);
    /* a refused image creates no file */
    snprintf(path, sizeof(path), "%s/refused.png", dir);
    remove(path);
    img.width = 0;
    CHECK(mpng_write_file(path, &img, MPNG_RGB) == MPNG_ERR_ARGS);
    f = fopen(path, "rb");
    CHECK(f == NULL);
    if (f) {
        fclose(f);
    }
}

int main(int argc, char **argv)
{
    test_crc();
    test_golden();
    test_shapes();
    test_block_edges();
    test_refusals();
    test_files(argc > 1 ? argv[1] : ".");
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("minimal-png %s: all checks passed\n", MPNG_VERSION);
    return 0;
}
