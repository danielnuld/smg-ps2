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
/* Rumble (Petari WPadRumble): MR::tryRumblePad* start a pattern (gRumblePatternTable's, in this order); 0 if it is
 * already playing. pad_update steps it (WPad::update); pad_rumble_stop clears every channel (WPadRumble::stop) */
enum { RUMBLE_VERY_STRONG_LONG, RUMBLE_VERY_STRONG, RUMBLE_STRONG, RUMBLE_MIDDLE, RUMBLE_WEAK, RUMBLE_VERY_WEAK,
       RUMBLE_SLIP_UP, RUMBLE_THROW, RUMBLE_HOPPER, RUMBLE_MORPH, RUMBLE_UNMORPH, RUMBLE_NUMB, RUMBLE_PRESS, RUMBLE_N };
int pad_rumble(int pattern);
int pad_rumble_pattern(const char *name); /* RumbleData::getPattern: its index by its (UTF-8) name, -1 none */
void pad_rumble_stop(void);
void pad_rumble_update(void);
void pad_replay_begin(void); /* REPLAY builds: host:replay.bin drives the pad from now on */

#endif
