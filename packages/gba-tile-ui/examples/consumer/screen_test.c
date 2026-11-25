/* Host test of the made-up screen: draw it, read the shadow map and OAM back. No console, no emulator. */
#include <stdio.h>

#include "screen.h"

static int failures;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static GtuUi ui;

int main(void)
{
    screen_init(&ui);
    screen_title(&ui, 1, 120);

    /* "MY GAME" is 7 characters: centred in 30 columns it starts at column 11; M is the 13th letter */
    CHECK(gtu_get_entry(&ui, 0, 11, 3) == ((1u << 12) | (MY_FONT_BASE + 12)));
    CHECK(gtu_get_entry(&ui, 0, 13, 3) == 0);                                 /* the space was skipped: the cleared tile 0 shows */
    CHECK(gtu_get_entry(&ui, 0, 14, 3) == ((1u << 12) | (MY_FONT_BASE + 6))); /* G */
    /* the selected row uses palette 2, the others palette 1 */
    CHECK((gtu_get_entry(&ui, 0, 11, 10) >> 12) == 2);
    CHECK((gtu_get_entry(&ui, 0, 12, 8) >> 12) == 1);
    /* the border is the outer ring of the 30 x 20 screen; the inside was cleared to tile 0 */
    CHECK(gtu_get_entry(&ui, 0, 0, 0) == MY_BORDER_TILE && gtu_get_entry(&ui, 0, 29, 19) == MY_BORDER_TILE);
    CHECK(gtu_get_entry(&ui, 0, 5, 5) == 0);
    /* the score is right-aligned in five columns at column 8, so "120" starts at column 10 */
    CHECK(gtu_get_entry(&ui, 0, 10, 17) == ((1u << 12) | (MY_FONT_BASE + 27)));
    /* the cursor sprite is at y = 56 + 16 */
    CHECK(ui.oam[0].attr0 == 72 && (ui.oam[0].attr1 & 0x1ff) == 40 && (ui.oam[0].attr2 & 0x3ff) == MY_CURSOR_TILE);
    CHECK(ui.oam[1].attr0 == GTU_ATTR0_DISABLED);
    /* both layers changed, so both are to be copied; taking the flags clears them */
    CHECK(gtu_take_map_dirty(&ui, 0) && !gtu_take_map_dirty(&ui, 0));
    CHECK(gtu_take_oam_dirty(&ui) && !gtu_take_oam_dirty(&ui));

    if (failures) return 1;
    puts("my-screens-test: ok");
    return 0;
}
