#ifndef PAD_H
#define PAD_H

#include <tamtypes.h>
#include <libpad.h>

int pad_init(void);
void pad_update(void);
int pad_held(u32 mask);
int pad_pressed(u32 mask);
/* Left stick, -1..1 (x right, y up); 0 inside the dead zone or in digital mode. */
float pad_lx(void);
float pad_ly(void);
/* Right stick, same convention: the star pointer's stand-in (game.c cursor) */
float pad_rx(void);
float pad_ry(void);

#endif
