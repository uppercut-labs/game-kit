/* A made-up project drawing a score line into a character canvas with bitmap-font. A real project's callback fills a
 * rectangle in its own renderer (a framebuffer, a sprite batch, a GPU quad); this one writes '#' into a text grid so
 * the result can be printed and checked.
 *
 *   my-bitmap-font
 */
#include <stdio.h>
#include <string.h>

#include "bfont/bfont.h"

#define W 40
#define H 16

typedef struct {
    char cells[H][W + 1];
} Canvas;

static void fill_rect(void *user, int x, int y, int w, int h)
{
    Canvas *canvas = (Canvas *)user;
    int i, j;

    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < W && j >= 0 && j < H) {
                canvas->cells[j][i] = '#';
            }
        }
    }
}

int main(void)
{
    Canvas canvas;
    int j, advanced, width;

    for (j = 0; j < H; j++) {
        memset(canvas.cells[j], '.', W);
        canvas.cells[j][W] = 0;
    }
    advanced = bfont_draw_text(&bfont_5x7, "Hi 10", 1, 1, 1, fill_rect, &canvas);   /* lowercase draws as capitals */
    width = bfont_text_width(&bfont_5x7, "Hi 10", 1);
    for (j = 0; j < H; j++) {
        puts(canvas.cells[j]);
    }
    printf("advanced %d, width %d\n", advanced, width);
    /* H, I, a space, 1 and 0 at 6 pixels each: five glyphs advance 30, the last one's gap is not part of the width */
    return advanced == 30 && width == 29 && canvas.cells[4][2] == '#' ? 0 : 1;
}
