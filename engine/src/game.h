#ifndef GAME_H
#define GAME_H

#include "blob.h"

void game_init(const Scene *s, const unsigned char *blob);
void game_update(void);
/* After the scene's models changed (zone packs swapped): find every placed model again. */
void game_rebind(void);
/* A stage Mario is close to whose pack is only loaded low (zone index), or -1. full[z]: stage z is loaded in full. */
int game_zone_wanted(const unsigned char *full);
/* The low-loaded zone Mario is heading for (a launch star's landing point), else game_zone_wanted; -1 for none. */
int game_zone_next(const unsigned char *full);
/* Test builds: put Mario just above the first model of a low-loaded zone. Returns its zone or -1. */
int game_teleport_low(const unsigned char *full);
/* Test builds: Mario caught by launch star i as if he spun there. Returns i, or -1 if there is none. */
int game_test_launch(int i);
void game_draw(void);
/* HUD numbers */
int game_coins(void);
int game_purple(void);
int game_purple_total(void);
int game_starpieces(void);
int game_kuribo_alive(void);
void game_debug(char *out, int n);
/* EE cycles spent in KCL queries since the last reset (frame-time split) */
extern unsigned int game_kcl_cycles;
static inline unsigned int cycles(void)
{
    unsigned int c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}

#endif
