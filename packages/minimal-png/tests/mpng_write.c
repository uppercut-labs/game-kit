/* Writes the images tests/check_with_zlib.py reads back:  mpng-write <rgb|rgba> <in: rgb|rgba> <width> <height> <file> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpng/mpng.h"

int main(int argc, char **argv)
{
    MpngLayout out, in;
    uint32_t w, h, x, y;
    uint8_t *px;
    MpngImage img;
    int rc;

    if (argc != 6) {
        return 2;
    }
    out = !strcmp(argv[1], "rgb") ? MPNG_RGB : MPNG_RGBA;
    in = !strcmp(argv[2], "rgb") ? MPNG_RGB : MPNG_RGBA;
    w = (uint32_t)atoi(argv[3]);
    h = (uint32_t)atoi(argv[4]);
    px = malloc((size_t)w * h * in);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) { /* the same function check_with_zlib.py computes */
            uint8_t *p = px + ((size_t)y * w + x) * in;

            p[0] = (uint8_t)(x * 7 + y * 3);
            p[1] = (uint8_t)(x ^ y);
            p[2] = (uint8_t)(x * y);
            if (in == MPNG_RGBA) {
                p[3] = (uint8_t)(255 - x - y);
            }
        }
    }
    img.pixels = px;
    img.width = w;
    img.height = h;
    img.stride = 0;
    img.layout = in;
    rc = mpng_write_file(argv[5], &img, out);
    free(px);
    return rc == MPNG_OK ? 0 : 1;
}
