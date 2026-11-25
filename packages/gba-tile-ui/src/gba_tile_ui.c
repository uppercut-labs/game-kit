/* gba-tile-ui: the core. Plain C, no console registers, no libgba. */
#include "gba_tile_ui/gba_tile_ui.h"

#include <string.h>

void gtu_init(GtuUi *ui)
{
    memset(ui, 0, sizeof *ui);
    ui->clip_columns = GTU_MAP_COLUMNS;
    ui->clip_rows = GTU_MAP_ROWS;
    ui->text_columns = GTU_VISIBLE_COLUMNS;
    ui->text_rows = GTU_VISIBLE_ROWS;
    gtu_hide_all_sprites(ui);
    gtu_mark_all_dirty(ui);
}

void gtu_set_font(GtuUi *ui, const char *glyphs, uint16_t font_base, uint16_t unknown_tile)
{
    ui->glyphs = glyphs;
    ui->font_base = font_base;
    ui->unknown_tile = unknown_tile;
}

void gtu_set_clip(GtuUi *ui, uint8_t columns, uint8_t rows)
{
    ui->clip_columns = columns > GTU_MAP_COLUMNS ? GTU_MAP_COLUMNS : columns;
    ui->clip_rows = rows > GTU_MAP_ROWS ? GTU_MAP_ROWS : rows;
}

void gtu_set_text_area(GtuUi *ui, uint8_t columns, uint8_t rows)
{
    ui->text_columns = columns > GTU_MAP_COLUMNS ? GTU_MAP_COLUMNS : columns;
    ui->text_rows = rows > GTU_MAP_ROWS ? GTU_MAP_ROWS : rows;
}

uint16_t gtu_entry(uint16_t tile, uint8_t palette, unsigned flags)
{
    uint16_t entry = (uint16_t)(tile & 0x03ffu);
    if (flags & GTU_HFLIP) entry |= 0x0400u;
    if (flags & GTU_VFLIP) entry |= 0x0800u;
    entry |= (uint16_t)((unsigned)(palette & 0x0fu) << 12);
    return entry;
}

uint16_t gtu_glyph_tile(const GtuUi *ui, char ch)
{
    const char *found;
    if (!ui->glyphs || ch == '\0') return ui->unknown_tile;
    found = strchr(ui->glyphs, ch);
    return found ? (uint16_t)(ui->font_base + (uint16_t)(found - ui->glyphs)) : ui->unknown_tile;
}

/* ---- maps ---- */

static void store(GtuUi *ui, unsigned bg, int cell, uint16_t entry)
{
    if (ui->maps[bg][cell] != entry) {
        ui->maps[bg][cell] = entry;
        ui->map_dirty[bg] = true;
    }
}

void gtu_clear_map(GtuUi *ui, unsigned bg, uint16_t tile, uint8_t palette)
{
    int cell;
    const uint16_t entry = gtu_entry(tile, palette, 0);
    if (bg >= GTU_MAP_COUNT) return;
    for (cell = 0; cell < GTU_MAP_CELLS; ++cell) store(ui, bg, cell, entry);
}

void gtu_set_entry(GtuUi *ui, unsigned bg, int x, int y, uint16_t entry)
{
    if (bg >= GTU_MAP_COUNT || x < 0 || y < 0 || x >= ui->clip_columns || y >= ui->clip_rows) return;
    store(ui, bg, y * GTU_MAP_COLUMNS + x, entry);
}

void gtu_set_tile(GtuUi *ui, unsigned bg, int x, int y, uint16_t tile, uint8_t palette, unsigned flags)
{
    gtu_set_entry(ui, bg, x, y, gtu_entry(tile, palette, flags));
}

uint16_t gtu_get_entry(const GtuUi *ui, unsigned bg, int x, int y)
{
    if (bg >= GTU_MAP_COUNT || x < 0 || y < 0 || x >= GTU_MAP_COLUMNS || y >= GTU_MAP_ROWS) return 0;
    return ui->maps[bg][y * GTU_MAP_COLUMNS + x];
}

void gtu_fill(GtuUi *ui, unsigned bg, int x, int y, int width, int height, uint16_t tile, uint8_t palette)
{
    int row, column;
    const uint16_t entry = gtu_entry(tile, palette, 0);
    for (row = 0; row < height; ++row)
        for (column = 0; column < width; ++column) gtu_set_entry(ui, bg, x + column, y + row, entry);
}

void gtu_load_map(GtuUi *ui, unsigned bg, const uint16_t *entries, size_t count)
{
    size_t i;
    if (bg >= GTU_MAP_COUNT || !entries) return;
    if (count > GTU_MAP_CELLS) count = GTU_MAP_CELLS;
    for (i = 0; i < count; ++i) store(ui, bg, (int)i, entries[i]);
}

/* ---- text ---- */

void gtu_text(GtuUi *ui, unsigned bg, int x, int y, const char *text, uint8_t palette)
{
    const int origin = x;
    if (!text || bg >= GTU_MAP_COUNT) return;
    for (; *text != '\0' && y < ui->text_rows; ++text) {
        if (*text == '\n') {
            x = origin;
            ++y;
            continue;
        }
        if (x >= 0 && x < ui->text_columns && y >= 0 && !(ui->skip_spaces && *text == ' '))
            gtu_set_tile(ui, bg, x, y, gtu_glyph_tile(ui, *text), palette, 0);
        ++x;
    }
}

void gtu_center(GtuUi *ui, unsigned bg, int y, const char *text, uint8_t palette)
{
    if (!text) return;
    gtu_text(ui, bg, ((int)ui->text_columns - (int)strlen(text)) / 2, y, text, palette);
}

void gtu_number(GtuUi *ui, unsigned bg, int x, int y, int value, int width, uint8_t palette)
{
    char digits[12];
    char text[24];
    int length = 0, i, pad;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    do {
        digits[length++] = (char)('0' + magnitude % 10u);
        magnitude /= 10u;
    } while (magnitude);
    if (value < 0) digits[length++] = '-';
    pad = width > length ? width - length : 0;
    if (pad > 12) pad = 12;
    for (i = 0; i < pad; ++i) text[i] = ' ';
    for (i = 0; i < length; ++i) text[pad + i] = digits[length - 1 - i];
    text[pad + length] = '\0';
    gtu_text(ui, bg, x, y, text, palette);
}

void gtu_clear_text_rect(GtuUi *ui, unsigned bg, int x, int y, int width, int height, uint8_t palette)
{
    gtu_fill(ui, bg, x, y, width, height, gtu_glyph_tile(ui, ' '), palette);
}

/* ---- sprites ---- */

static void store_obj(GtuUi *ui, unsigned index, uint16_t a0, uint16_t a1, uint16_t a2)
{
    GtuObj *o = &ui->oam[index];
    if (o->attr0 != a0 || o->attr1 != a1 || o->attr2 != a2 || o->fill != 0) {
        o->attr0 = a0;
        o->attr1 = a1;
        o->attr2 = a2;
        o->fill = 0;
        ui->oam_dirty = true;
    }
}

void gtu_hide_sprite(GtuUi *ui, unsigned index)
{
    if (index >= GTU_OAM_COUNT) return;
    store_obj(ui, index, GTU_ATTR0_DISABLED, 0, 0);
}

void gtu_hide_all_sprites(GtuUi *ui)
{
    unsigned i;
    for (i = 0; i < GTU_OAM_COUNT; ++i) gtu_hide_sprite(ui, i);
}

void gtu_set_sprite(GtuUi *ui, unsigned index, int x, int y, uint16_t tile, uint8_t palette, GtuSpriteSize size,
                    uint8_t priority, unsigned flags)
{
    uint16_t a0, a1, a2;
    if (index >= GTU_OAM_COUNT) return;
    a0 = (uint16_t)(y & 0x00ff);
    a1 = (uint16_t)(((unsigned)size & 3u) << 14 | ((unsigned)x & 0x01ffu));
    if (flags & GTU_HFLIP) a1 |= 0x1000u;
    if (flags & GTU_VFLIP) a1 |= 0x2000u;
    a2 = (uint16_t)((tile & 0x03ffu) | ((unsigned)(priority & 3u) << 10) | ((unsigned)(palette & 15u) << 12));
    store_obj(ui, index, a0, a1, a2);
}

/* ---- dirty flags ---- */

bool gtu_take_map_dirty(GtuUi *ui, unsigned bg)
{
    bool was;
    if (bg >= GTU_MAP_COUNT) return false;
    was = ui->map_dirty[bg];
    ui->map_dirty[bg] = false;
    return was;
}

bool gtu_take_oam_dirty(GtuUi *ui)
{
    const bool was = ui->oam_dirty;
    ui->oam_dirty = false;
    return was;
}

void gtu_mark_all_dirty(GtuUi *ui)
{
    unsigned bg;
    for (bg = 0; bg < GTU_MAP_COUNT; ++bg) ui->map_dirty[bg] = true;
    ui->oam_dirty = true;
}
