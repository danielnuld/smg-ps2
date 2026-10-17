#ifndef MENU_H
#define MENU_H

#include "blob.h"

/* Screens outside the game's action (Petari src/Game/Screen): the pause menu and its confirm window so far.
 * game.c calls menu_update at the start of each logic step (1: the game is paused, its world stops) and menu_draw
 * last in the frame. */
void menu_init(const Scene *s);
int menu_update(void);
void menu_draw(void);
int menu_paused(void);
/* a fade to black over `frames`, then seq_galaxy_move (closeSystemWipe*, then the request) */
void menu_fade_move(int frames, int type, const char *stage, int scenario, int start_id, int start_zone);
int menu_fading(void);
/* ScenarioSelectScene: the galaxy's scenario chosen over the frozen stage, then the move there (sequence.c) */
void menu_scenario_select(const char *galaxy);
int menu_scenario_selecting(void);
int menu_draw_screen(void); /* game_draw: 1 when a whole-screen menu (the scenario select) drew the frame */
const u16 *msg_get(const char *label); /* MR::getLayoutMessageDirect / getGameMessageDirect (MSGS chunk), NULL none */
int menu_test_point_scenario(const char *pane);
int menu_test_point(const char *pane); /* tests: the pointer on a pane of the open pause menu / its window */

/* game.c */
int game_pause_permitted(void);     /* GameScene::isPermitToPauseMenu */
void game_cursor_menu(void);        /* the pointer moved by the right stick and shown (startStarPointerModePauseMenu) */
int game_scenario_stars(int n);     /* hasPowerStarInCurrentStage(n) */

#endif
