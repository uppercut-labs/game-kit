/* A stand-in for libgba's <gba.h> with just what gba_tile_ui_gba.h uses, so the glue is compiled and run on a host.
 * Written for this test; it is not libgba and holds none of its text. VRAM is a plain array, a screen block is 2 KB
 * (0x800 bytes) into it, and dmaCopy is a memcpy that counts its calls. */
#ifndef FAKE_GBA_H
#define FAKE_GBA_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern uint8_t fake_vram[0x10000];
extern uint8_t fake_oam[1024];
extern unsigned fake_dma_calls;

#define MAP_BASE_ADR(n) ((void *)(fake_vram + (size_t)(n) * 0x800u))
#define OAM ((void *)fake_oam)

static inline void dmaCopy(const void *source, void *destination, uint32_t bytes)
{
    memcpy(destination, source, bytes);
    ++fake_dma_calls;
}

#endif
