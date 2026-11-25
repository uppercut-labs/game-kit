/* A made-up game scene: a score line, a cursor and a beep. It is what a game's logic looks like to the stub. */
#ifndef MY_GAME_H
#define MY_GAME_H

#include <genesis.h>

void game_init(void);
void game_frame(void); /* one frame: read the pad, move the cursor, draw, wait for vblank */
u16 game_score(void);

extern const u8 beep_sample[64];

#endif
