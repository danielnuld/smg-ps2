#ifndef GAME_H
#define GAME_H

#include "blob.h"

void game_init(const Scene *s, const unsigned char *blob);
void game_update(void);
/* After the scene's models changed (zone packs swapped): find every placed model again. */
void game_rebind(void);
/* A stage Mario is close to whose pack is only loaded low (zone index), or -1. full[z]: stage z is loaded in full. */
int game_zone_wanted(const unsigned char *full);
/* The low-loaded zone Mario is heading for (a launch star's landing point, else the nearest within 5000); -1. */
int game_zone_next(const unsigned char *full);
/* Test builds: put Mario just above the first model of a low-loaded zone. Returns its zone or -1. */
int game_teleport_low(const unsigned char *full);
/* Test builds: Mario caught by launch star i as if he spun there. Returns i, or -1 if there is none. */
int game_test_launch(int i);
/* Test builds: Mario caught by the first SpinDriver (small 1) or SuperSpinDriver (0). Returns its launch index or -1;
 * game_launch_state() gives the launch holding Mario (-1 none). */
int game_test_spindriver(int small);
int game_launch_state(void);
/* Test builds: Mario just above the first breakable box, spinning. Returns its OBJS row or -1. */
int game_test_box(void);
/* Test builds: Mario spinning on the cage (or other declarer) holding the first event power star, its row (-2 it is
 * gone, -1 none); on_star: Mario put on that star instead, returns its star_pos state (3: can be taken) */
int game_test_cage(int on_star);
/* Test builds: Mario put inside RestartCube k, its arg0 (MarioNo) or -1; k -1: a miss */
int game_test_restart(int k);
/* Test builds: Mario put 600 in front of the first cannon launcher's muzzle (his touch sphere on its line); its row */
int game_test_cannon(void);
/* Test builds: Mario walking through the k-th member of the plant groups holding items, its group row; -2 the group's
 * members are not placed yet (Mario put over it), -1 no more */
int game_test_plant(int k);
/* Test builds: Mario held in the first LavaSteam's vent (his touch sphere 100 up it); its row or -1 */
int game_test_steam(void);
/* Test builds: Mario spinning on the first breakable whose dead switch makes a StarPieceSpot burst; its row or -1 */
int game_test_spot(void);
/* Test builds: Mario jumping into the first ItemBlock from below. Returns its row or -1. */
int game_test_block(void);
int game_test_enemy(int kind, int k, int how); /* kind: 0 Kuribo, 1 TakoHei, 2 PackunPetit, 3 KoteBug, 4 JumpSpider */
int game_kind_alive(int kind);
/* Test builds: Mario dropped onto the first CollapsePlane. Returns its OBJS row or -1; game_test_collapse_step(row)
 * gives that plane's collapse frame (0 not started) or -1 once it has vanished. */
int game_test_collapse(void);
/* Test builds: Mario put next to the first SpaceMine (kind 1) or NeedlePlant (2). Returns its OBJS row or -1. */
int game_test_hazard(int kind);
/* Test builds: Mario put on the first PowerStar / 1-up / life-up mushroom left. Returns its kind (1-3) or 0. */
int game_test_item(void);
/* Test builds: Mario put on the first ElectricRail segment (0, or -1 if none) / into the first black hole (its row). */
int game_test_spark(void);
/* Test builds: Mario put on the third ball of the first FireBar. Returns its row or -1. */
int game_test_firebar(void);
/* Test builds: Mario dropped onto the first rotating (rail 0) or rail-moving (1) map part, its row or -1; that
 * row's angle / distance along its rail */
int game_test_rotor(int rail);
float game_rotor_angle(int row);
float game_rail_coord(int row);
/* Test builds: Mario on the rail 300 ahead of the first rolling rock (its RockCreator row, or -1); rocks rolling */
int game_test_rock(void);
int game_test_rockcreator(void); /* Mario at the start of the first RockCreator's rail */
int game_rocks_alive(void);
/* Test builds: Mario put on the k-th FlipPanel. Returns its row or -1 past the last. */
int game_test_flip(int k);
int game_flips_back(void); /* FlipPanels showing their back */
/* Test builds: Mario dropped onto the first EarthenPipe that has a partner. Returns its index or -1. */
int game_test_pipe(void);
/* Test builds: Mario 500 above the first hip drop switch (else HipDropRock, else breakable), starting a hip drop. Returns its row or -1;
 * game_hip_state() gives the hip drop phase (0 none, 1 start, 2 fall, 3 landing). */
int game_test_hip(void);
int game_hip_state(void);
int game_test_hole(void);
int game_hurt_timer(void);
int game_test_collapse_step(int row);
void game_draw(void);
/* HUD numbers */
int game_coins(void);
int game_life(void);
int game_lives(void);
int game_stars(void);
int game_misses(void);
int game_purple(void);
int game_purple_total(void);
int game_starpieces(void);
int game_pieces_out(void); /* star pieces out now (not taken, appear switch on) */
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
