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
/* AstroDomeGalaxySelector's layouts, driven by game.c's dome select */
void menu_dome_open(void);
void menu_dome_close(void);
void menu_dome_info(const char *galaxy, int state);  /* GalaxySelectInfo::show (state 0 hatena .. 3 open), each frame */
void menu_dome_back(int op);         /* GalaxySelectBackButton: 0 appear, 1 disappear, 2 decide, 3 tryPointing */
int menu_dome_back_state(int q);     /* 0 isPointing, 1 isAppearing, 2 isDisappearing (or gone), 3 gone */
void menu_dome_confirm_appear(void); /* GalaxyConfirmLayout */
int menu_dome_confirm(void);         /* 0 choosing, 1 yes, 2 no */
void menu_dome_plate(int i, const char *galaxy, int type, int immediate, float x, float y); /* GalaxyNamePlate */
/* the miss / game over sequences' layouts (game.c miss_*) */
void menu_miss_start(void);          /* MissLayout ("TooBad") */
void menu_wipe_koopa(int frames);    /* WipeKoopa: closes over frames (<= 0: 90) */
int menu_wipe_koopa_closed(void);
void menu_wipe_gameover(void);       /* WipeGameOver ("GameOver") */
int menu_file_selecting(void);       /* the FileSelect stage's FileSelector running */
void menu_save_request(void);        /* the files changed: the save is to be written */
int menu_take_save_request(void);    /* main.c: 1 once per request */
int menu_draw_screen(void); /* game_draw: 1 when a whole-screen menu (the scenario select) drew the frame */
const u16 *msg_get(const char *label); /* MR::getLayoutMessageDirect / getGameMessageDirect (MSGS chunk), NULL none */
int menu_test_point_scenario(const char *pane);
int menu_test_point_dome(const char *pane);
int menu_test_point_file(int slot, const char *pane);
int menu_test_point(const char *pane); /* tests: the pointer on a pane of the open pause menu / its window */

/* game.c */
int game_pause_permitted(void);     /* GameScene::isPermitToPauseMenu */
void game_cursor_menu(void);        /* the pointer moved by the right stick and shown (startStarPointerModePauseMenu) */
int game_scenario_stars(int n);     /* hasPowerStarInCurrentStage(n) */

#endif
