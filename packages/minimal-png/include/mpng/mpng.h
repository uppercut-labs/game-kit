/* minimal-png: write a PNG with no zlib. The pixel data goes out as stored (uncompressed) deflate blocks, so the file
 * is larger than a compressed one and every PNG reader still opens it.
 *
 *   MpngImage img = { pixels, width, height, 0, MPNG_RGBA };
 *   mpng_write_file("frame.png", &img, MPNG_RGB);      // alpha dropped
 *
 * Input is 8-bit RGB or RGBA. Output is 8-bit RGB (colour type 2) or RGBA (colour type 6). No dependencies beyond the C
 * library; a call is thread safe (nothing is shared between calls).
 */
#ifndef MPNG_H
#define MPNG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MPNG_VERSION "1.0.0"

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes per pixel, so the value is also the layout's size. */
typedef enum { MPNG_RGB = 3, MPNG_RGBA = 4 } MpngLayout;

typedef struct {
    const uint8_t *pixels; /* the first byte of the top row */
    uint32_t width, height;
    size_t stride;         /* bytes from one row to the next; 0 means width * bytes per pixel (rows packed) */
    MpngLayout layout;     /* how the pixels are stored */
} MpngImage;

enum {
    MPNG_OK = 0,
    MPNG_ERR_ARGS = -1,   /* a NULL pointer, a bad layout, zero width or height, or a stride shorter than a row */
    MPNG_ERR_SIZE = -2,   /* the image is bigger than a PNG or this system can hold */
    MPNG_ERR_MEMORY = -3, /* out of memory */
    MPNG_ERR_IO = -4      /* the file could not be opened, written or closed */
};

/* The length of the file mpng_encode makes, or 0 when the arguments are refused (the same cases as MPNG_ERR_ARGS and
   MPNG_ERR_SIZE). Handy for a caller that wants to place the file in a buffer of its own. */
size_t mpng_encoded_size(uint32_t width, uint32_t height, MpngLayout out);

/* Encode into a new buffer: *png and *len are set on success (0) and the caller releases the buffer with mpng_free.
   On a failure both are left as they were, and the result is one of MPNG_ERR_*. `out` is the layout of the file:
   RGBA to RGB drops the alpha; RGB to RGBA makes every pixel opaque. */
int mpng_encode(const MpngImage *img, MpngLayout out, uint8_t **png, size_t *len);
void mpng_free(void *png);

/* Encode and write. Both close nothing they were not given: mpng_write_stream leaves the stream open (and unflushed
   past the write), mpng_write_file opens path for writing, writes, and closes it (a close failure is MPNG_ERR_IO). A UTF-8
   path works on Windows. A file that could not be written whole is removed again. */
int mpng_write_stream(FILE *f, const MpngImage *img, MpngLayout out);
int mpng_write_file(const char *path, const MpngImage *img, MpngLayout out);

const char *mpng_error_text(int rc);

/* The CRC-32 PNG uses (the one of zlib's crc32), for a caller that checks its own chunks. */
uint32_t mpng_crc32(uint32_t crc, const uint8_t *data, size_t n);

#ifdef __cplusplus
}
#endif

#endif
