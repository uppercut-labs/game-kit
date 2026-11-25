/* minimal-png (mpng.h). */
#include "mpng/mpng.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define STORED_MAX 65535u
#define PNG_MAX_DIM 0x7FFFFFFFu

/* CRC-32 by nibbles: a 16-entry table, so there is nothing to build at run time and nothing shared between threads. */
static const uint32_t CRC_NIBBLE[16] = { 0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                         0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                         0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };

uint32_t mpng_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = CRC_NIBBLE[crc & 15u] ^ (crc >> 4);
        crc = CRC_NIBBLE[crc & 15u] ^ (crc >> 4);
    }
    return ~crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* the size of the pieces of the file, or 0 when they do not fit */
typedef struct {
    size_t row;     /* a scanline: filter byte plus pixels */
    size_t raw;     /* every scanline */
    size_t blocks;  /* stored blocks */
    size_t zlen;    /* the zlib stream */
    size_t total;   /* the file */
} Sizes;

static int sizes_for(uint32_t w, uint32_t h, MpngLayout out, Sizes *s)
{
    size_t bpp = (size_t)out;

    if (w == 0 || h == 0 || w > PNG_MAX_DIM || h > PNG_MAX_DIM || (out != MPNG_RGB && out != MPNG_RGBA)) {
        return MPNG_ERR_ARGS;
    }
    /* every product is checked against half the range, so the sums below cannot wrap either */
    if ((size_t)w > (SIZE_MAX / 2) / bpp - 1) {
        return MPNG_ERR_SIZE;
    }
    s->row = (size_t)w * bpp + 1;
    if ((size_t)h > (SIZE_MAX / 2) / s->row) {
        return MPNG_ERR_SIZE;
    }
    s->raw = s->row * h;
    s->blocks = (s->raw + STORED_MAX - 1) / STORED_MAX;
    s->zlen = 2 + s->raw + 5 * s->blocks + 4;
    if (s->zlen > 0x7FFFFFFFu) { /* one IDAT chunk holds at most 2^31 - 1 bytes */
        return MPNG_ERR_SIZE;
    }
    s->total = 8 + (12 + 13) + (12 + s->zlen) + 12;
    return MPNG_OK;
}

size_t mpng_encoded_size(uint32_t width, uint32_t height, MpngLayout out)
{
    Sizes s;

    return sizes_for(width, height, out, &s) == MPNG_OK ? s.total : 0;
}

/* The raw scanline stream is cut into stored blocks of up to 65535 bytes, each with a 5-byte header. The writer puts
   the bytes straight into their place in the output, leaving each header's room, so the image is not copied twice. */
typedef struct {
    uint8_t *z;      /* start of the zlib data (after the two header bytes) */
    size_t pos;      /* raw bytes written so far */
    size_t raw;      /* raw bytes in all */
    uint32_t a, b;   /* Adler-32 */
} Zout;

static void z_put(Zout *o, const uint8_t *src, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) { /* the checksum, a byte at a time: a row is short next to a block */
        o->a += src[i];
        if (o->a >= 65521u) {
            o->a -= 65521u;
        }
        o->b += o->a;
        if (o->b >= 65521u) {
            o->b -= 65521u;
        }
    }
    while (n) {
        size_t in_block = o->pos % STORED_MAX, room = STORED_MAX - in_block, take = n < room ? n : room;
        size_t block = o->pos / STORED_MAX;
        uint8_t *dst = o->z + block * (STORED_MAX + 5) + 5 + in_block;

        memcpy(dst, src, take);
        o->pos += take;
        src += take;
        n -= take;
    }
}

static void z_headers(Zout *o)
{
    size_t blocks = (o->raw + STORED_MAX - 1) / STORED_MAX, k;

    for (k = 0; k < blocks; k++) {
        size_t left = o->raw - k * STORED_MAX, n = left < STORED_MAX ? left : STORED_MAX;
        uint8_t *h = o->z + k * (STORED_MAX + 5);

        h[0] = (uint8_t)(k + 1 == blocks);
        h[1] = (uint8_t)n;
        h[2] = (uint8_t)(n >> 8);
        h[3] = (uint8_t)~n;
        h[4] = (uint8_t)(~n >> 8);
    }
}

int mpng_encode(const MpngImage *img, MpngLayout out, uint8_t **png, size_t *len)
{
    Sizes s;
    size_t in_bpp, packed, stride, y;
    uint32_t x;
    uint8_t *buf, *row, *p;
    Zout z;
    int rc;

    if (!img || !png || !len || !img->pixels || (img->layout != MPNG_RGB && img->layout != MPNG_RGBA)) {
        return MPNG_ERR_ARGS;
    }
    rc = sizes_for(img->width, img->height, out, &s);
    if (rc != MPNG_OK) {
        return rc;
    }
    in_bpp = (size_t)img->layout;
    if ((size_t)img->width > SIZE_MAX / in_bpp) {
        return MPNG_ERR_SIZE;
    }
    packed = (size_t)img->width * in_bpp;
    stride = img->stride ? img->stride : packed;
    if (stride < packed) {
        return MPNG_ERR_ARGS;
    }
    buf = malloc(s.total);
    row = malloc(s.row);
    if (!buf || !row) {
        free(buf);
        free(row);
        return MPNG_ERR_MEMORY;
    }

    /* signature and IHDR */
    memcpy(buf, "\x89PNG\r\n\x1a\n", 8);
    p = buf + 8;
    put_be32(p, 13);
    memcpy(p + 4, "IHDR", 4);
    put_be32(p + 8, img->width);
    put_be32(p + 12, img->height);
    p[16] = 8;
    p[17] = out == MPNG_RGB ? 2 : 6;
    p[18] = p[19] = p[20] = 0;
    put_be32(p + 21, mpng_crc32(0, p + 4, 17));

    /* IDAT: zlib header, stored blocks, Adler-32 */
    p += 25;
    put_be32(p, (uint32_t)s.zlen);
    memcpy(p + 4, "IDAT", 4);
    p[8] = 0x78;
    p[9] = 0x01;
    z.z = p + 10;
    z.pos = 0;
    z.raw = s.raw;
    z.a = 1;
    z.b = 0;
    row[0] = 0; /* filter: none */
    for (y = 0; y < img->height; y++) {
        const uint8_t *src = img->pixels + y * stride;
        uint8_t *d = row + 1;

        if (img->layout == out) {
            memcpy(d, src, packed);
        } else if (img->layout == MPNG_RGBA) { /* RGBA to RGB */
            for (x = 0; x < img->width; x++, src += 4, d += 3) {
                d[0] = src[0], d[1] = src[1], d[2] = src[2];
            }
        } else { /* RGB to RGBA */
            for (x = 0; x < img->width; x++, src += 3, d += 4) {
                d[0] = src[0], d[1] = src[1], d[2] = src[2], d[3] = 0xFF;
            }
        }
        z_put(&z, row, s.row);
    }
    z_headers(&z);
    put_be32(z.z + s.raw + 5 * s.blocks, z.b << 16 | z.a);
    put_be32(p + 8 + s.zlen, mpng_crc32(0, p + 4, 4 + s.zlen));
    p += 12 + s.zlen;

    /* IEND */
    put_be32(p, 0);
    memcpy(p + 4, "IEND", 4);
    put_be32(p + 8, mpng_crc32(0, p + 4, 4));

    free(row);
    *png = buf;
    *len = s.total;
    return MPNG_OK;
}

void mpng_free(void *png)
{
    free(png);
}

int mpng_write_stream(FILE *f, const MpngImage *img, MpngLayout out)
{
    uint8_t *png = NULL;
    size_t len = 0;
    int rc;

    if (!f) {
        return MPNG_ERR_ARGS;
    }
    rc = mpng_encode(img, out, &png, &len);
    if (rc != MPNG_OK) {
        return rc;
    }
    rc = fwrite(png, 1, len, f) == len ? MPNG_OK : MPNG_ERR_IO;
    mpng_free(png);
    return rc;
}

#ifdef _WIN32
static FILE *open_for_write(const char *path)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;
    FILE *f = NULL;

    if (w && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) > 0) {
        f = _wfopen(w, L"wb");
    }
    free(w);
    return f;
}

static void remove_file(const char *path)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    wchar_t *w = n > 0 ? malloc((size_t)n * sizeof(wchar_t)) : NULL;

    if (w && MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n) > 0) {
        _wremove(w);
    }
    free(w);
}
#else
static FILE *open_for_write(const char *path)
{
    return fopen(path, "wb");
}

static void remove_file(const char *path)
{
    remove(path);
}
#endif

int mpng_write_file(const char *path, const MpngImage *img, MpngLayout out)
{
    uint8_t *png = NULL;
    size_t len = 0;
    FILE *f;
    int rc;

    if (!path || !*path) {
        return MPNG_ERR_ARGS;
    }
    rc = mpng_encode(img, out, &png, &len); /* encode first: a refused image leaves no empty file behind */
    if (rc != MPNG_OK) {
        return rc;
    }
    f = open_for_write(path);
    if (!f) {
        mpng_free(png);
        return MPNG_ERR_IO;
    }
    rc = fwrite(png, 1, len, f) == len ? MPNG_OK : MPNG_ERR_IO;
    if (fclose(f) != 0) {
        rc = MPNG_ERR_IO;
    }
    mpng_free(png);
    if (rc != MPNG_OK) {
        remove_file(path);
    }
    return rc;
}

const char *mpng_error_text(int rc)
{
    switch (rc) {
    case MPNG_OK:
        return "ok";
    case MPNG_ERR_ARGS:
        return "bad arguments (a NULL pointer, a bad layout, a zero size or a short stride)";
    case MPNG_ERR_SIZE:
        return "the image is too big for a PNG or for this system";
    case MPNG_ERR_MEMORY:
        return "out of memory";
    case MPNG_ERR_IO:
        return "could not write the file";
    default:
        return "unknown error";
    }
}
