#ifndef GAME_H
#define GAME_H

#include "blob.h"

void game_init(const Scene *s, const unsigned char *blob);
void game_update(void);
void game_draw(void);
/* HUD numbers */
int game_coins(void);
int game_purple(void);
int game_purple_total(void);
int game_starpieces(void);
int game_kuribo_alive(void);

#endif
