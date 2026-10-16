#ifndef PAD_H
#define PAD_H

#include <tamtypes.h>
#include <libpad.h>

int pad_init(void);
void pad_update(void);
int pad_held(u32 mask);
int pad_pressed(u32 mask);
/* This frame's press of these buttons taken (a talk started by A: Mario does not jump) */
void pad_eat(u32 mask);
extern u32 pad_force;
extern int pad_force_stick;
extern float pad_force_lx, pad_force_ly;
/* Left stick, -1..1 (x right, y up); 0 inside the dead zone or in digital mode. */
float pad_lx(void);
float pad_ly(void);
/* Right stick, same convention: the star pointer's stand-in (game.c cursor) */
float pad_rx(void);
float pad_ry(void);
void pad_replay_begin(void); /* REPLAY builds: host:replay.bin drives the pad from now on */

#endif
