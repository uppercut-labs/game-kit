/* Software frames into top-down RGB24, for the video capture. */
#include <string.h>

#include "lgp/lgp.h"

static uint8_t five(unsigned v)
{
    return (uint8_t)((v << 3) | (v >> 2));
}

int lgp_convert_rgb24(unsigned format, const void *pixels, unsigned width, unsigned height, size_t pitch, uint8_t *rgb)
{
    unsigned x, y;
    if (format > 2u) {
        return 0;
    }
    for (y = 0; y < height; ++y) {
        const uint8_t *row = (const uint8_t *)pixels + (size_t)y * pitch;
        uint8_t *out = rgb + (size_t)y * width * 3u;
        for (x = 0; x < width; ++x) {
            if (format == 1u) { /* XRGB8888 */
                uint32_t p;
                memcpy(&p, row + 4u * x, sizeof(p));
                out[0] = (uint8_t)(p >> 16);
                out[1] = (uint8_t)(p >> 8);
                out[2] = (uint8_t)p;
            } else {
                uint16_t p;
                memcpy(&p, row + 2u * x, sizeof(p));
                if (format == 2u) { /* RGB565 */
                    unsigned g = (p >> 5) & 0x3fu;
                    out[0] = five((p >> 11) & 0x1fu);
                    out[1] = (uint8_t)((g << 2) | (g >> 4));
                    out[2] = five(p & 0x1fu);
                } else { /* 0RGB1555 */
                    out[0] = five((p >> 10) & 0x1fu);
                    out[1] = five((p >> 5) & 0x1fu);
                    out[2] = five(p & 0x1fu);
                }
            }
            out += 3;
        }
    }
    return 1;
}
