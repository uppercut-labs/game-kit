/* A made-up port's frame dump: a gradient written as an RGB PNG, then read back to check the signature.
 *
 *   my-png-dump <scratch dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpng/mpng.h"

#define W 160
#define H 90

int main(int argc, char **argv)
{
    static uint8_t frame[W * H * 4]; /* what a renderer read back: RGBA */
    char path[1024];
    unsigned char sig[8];
    FILE *f;
    int x, y, rc;
    MpngImage img = { frame, W, H, 0, MPNG_RGBA };

    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            uint8_t *p = frame + ((size_t)y * W + x) * 4;

            p[0] = (uint8_t)(x * 255 / (W - 1));
            p[1] = (uint8_t)(y * 255 / (H - 1));
            p[2] = 128;
            p[3] = 255;
        }
    }
    snprintf(path, sizeof(path), "%s/frame.png", argc > 1 ? argv[1] : ".");
    rc = mpng_write_file(path, &img, MPNG_RGB); /* alpha dropped */
    if (rc != MPNG_OK) {
        fprintf(stderr, "cannot write %s: %s\n", path, mpng_error_text(rc));
        return 1;
    }
    printf("minimal-png %s wrote %s (%zu bytes)\n", MPNG_VERSION, path, mpng_encoded_size(W, H, MPNG_RGB));
    f = fopen(path, "rb");
    if (!f || fread(sig, 1, 8, f) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) {
        fprintf(stderr, "not a PNG\n");
        return 1;
    }
    fclose(f);
    return 0;
}
