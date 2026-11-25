/* A made-up title screen: what a game's screen code looks like with gba-tile-ui. No console header is included, so the
 * file builds on the host and in a devkitARM build alike. */
#ifndef MY_SCREEN_H
#define MY_SCREEN_H

#include "gba_tile_ui/gba_tile_ui.h"

enum { MY_FONT_BASE = 64, MY_BORDER_TILE = 3, MY_CURSOR_TILE = 200 };

void screen_init(GtuUi *ui);
void screen_title(GtuUi *ui, int selected, int best_score);

#endif
