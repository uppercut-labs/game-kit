/* The libgba glue for gba-tile-ui: copies what changed to VRAM and OAM. Optional: the core does not include it, and it
 * is the only file here that needs libgba (devkitARM). A game that sets up its own DMA can ignore it and use
 * gtu_take_map_dirty and gtu_take_oam_dirty directly. */
#ifndef GBA_TILE_UI_GBA_H
#define GBA_TILE_UI_GBA_H

#include <gba.h>

#include "gba_tile_ui.h"

/* Call once a frame, in the vertical blank. Map bg goes to screen block first_screen_block + bg, so a game whose four
 * maps are at screen blocks 28 to 31 passes 28. A map or the OAM that did not change since the last present is not
 * copied. */
static inline void gtu_present_gba(GtuUi *ui, unsigned first_screen_block)
{
    unsigned bg;
    for (bg = 0; bg < GTU_MAP_COUNT; ++bg) {
        if (gtu_take_map_dirty(ui, bg)) dmaCopy(ui->maps[bg], MAP_BASE_ADR(first_screen_block + bg), GTU_MAP_BYTES);
    }
    if (gtu_take_oam_dirty(ui)) dmaCopy(ui->oam, OAM, GTU_OAM_BYTES);
}

#endif
