#include "screen.h"

static const char GLYPHS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:!- ";

void screen_init(GtuUi *ui)
{
    gtu_init(ui);
    /* characters outside GLYPHS draw the last tile (a box), at MY_FONT_BASE + 40 */
    gtu_set_font(ui, GLYPHS, MY_FONT_BASE, MY_FONT_BASE + 40);
    ui->skip_spaces = true; /* text lets the background show through */
    gtu_set_clip(ui, GTU_VISIBLE_COLUMNS, GTU_VISIBLE_ROWS);
}

void screen_title(GtuUi *ui, int selected, int best_score)
{
    static const char *const items[] = {"START", "OPTIONS", "QUIT"};
    int i;
    gtu_clear_map(ui, 0, MY_BORDER_TILE, 0);
    gtu_fill(ui, 0, 1, 1, 28, 18, 0, 0);
    gtu_center(ui, 0, 3, "MY GAME", 1);
    for (i = 0; i < 3; ++i) gtu_center(ui, 0, 8 + i * 2, items[i], i == selected ? 2 : 1);
    gtu_text(ui, 0, 2, 17, "BEST:", 1);
    gtu_number(ui, 0, 8, 17, best_score, 5, 1);
    gtu_hide_all_sprites(ui);
    gtu_set_sprite(ui, 0, 40, 56 + selected * 16, MY_CURSOR_TILE, 0, GTU_SPRITE_16, 0, 0);
}
