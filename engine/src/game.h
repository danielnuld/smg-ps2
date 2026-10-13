#ifndef GAME_H
#define GAME_H

#include "blob.h"

void game_init(const Scene *s, const unsigned char *blob);
void game_set_time_limit(int seconds);
int game_time_left(void);
void game_update(void);
/* After the scene's models changed (zone packs swapped): find every placed model again. */
void game_rebind(void);
/* full[z]: stage z is loaded in full. need[z]: Mario needs its collision (2: at once); soon[z]: he is heading for it; dist[z]:
 * its priority (smaller first), for the zones that fit the memory room (main.c stream_update). */
void game_zone_sets(const unsigned char *full, unsigned char *need, unsigned char *soon, float *dist);
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
int game_test_kgun(void);
int game_test_orock(void);
int game_test_hhp(void);
int game_test_vine(void);
int game_test_petari(void);
int game_test_seesaw(int phase);
int game_test_follow(int phase);
int game_test_replica(int phase);
int game_test_invinc(void);
int game_test_slip(void);
int game_test_talk(int frame);
/* EggStar 2 (isStageStarPieceFollowGroupLimit): StarPieceFollowGroups give at most 100 minus Mario's star pieces */
void game_set_piece_follow_limit(int on);
int game_test_ibubble(int touch);
int game_test_kgun_state(void);
/* Test builds: Mario held in the first LavaSteam's vent (his touch sphere 100 up it); its row or -1 */
int game_test_steam(void);
/* Test builds: Mario held in the first LavaGeyser's column (his centre 300 up it); its row or -1 */
int game_test_geyser(void);
/* Test builds: Mario held on the first Birikyu's path; its row, -2 streaming, -1 none */
int game_test_biri(void);
/* Test builds: Mario spinning by (0) / hip dropping onto (1) the first PunchingKinoko; its row, -2 streaming,
 * -1 none. game_punch_state: its phase (row_turn) and its head's distance from its place */
int game_test_punch(int how);
/* Test builds: Mario spinning by (0) / running into (1) the first waiting CocoNut (its row, -2 streaming, -1 none);
 * game_coco_state: the first moving one's phase and distance from its place */
int game_test_coco(int how);
/* Test builds: SpaceCocoon steps (game.c game_test_cocoon) */
int game_test_cocoon(int phase, float drag);
/* Test builds: Fluff steps (game.c game_test_fluff) */
int game_test_fluff(int phase);
int game_test_waterplant(void);
/* Test builds: StinkBugSmall (game.c game_test_stink) */
int game_test_stink(int how);
int game_test_unizo(void);
int game_test_poihana(int how);
int game_poihana_state(void);
int game_test_note(int phase);
int game_test_key(void);
int game_test_jet(int phase);
int game_test_screw(void);
int game_test_candle(int how);
int game_test_fountain(void);
int game_test_stray(void);
int game_test_swim(void);
int game_swim_state(void);
int game_test_walker(const char *cls, int k);
int game_test_route(const char *cls);
int game_test_star_near(void);
int game_test_purple(void);
int game_test_ota(void);
int game_test_fat(void);
void game_test_fat_nodrop(void);
int game_test_dino(void);
int game_test_bubble(void);
int game_test_fire(int phase);
int game_test_bee(int phase);
void game_test_jump(void);
int game_bee_state(void);
int game_fire_state(void);
void game_test_spin_now(void);
int game_screw_state(void);
int game_jet_state(void);
int game_key_taken(void);
int game_unizo_state(int *dist);
int game_stink_state(int *dist);
int game_coco_state(int *dist);
int game_punch_state(int *dist);
/* Test builds: Mario at the first ungiven DeadLeaves (1, spinning) / Pompon plant (2, into its flower); its row,
 * -2 streaming, -1 none */
int game_test_giver(int kind);
/* Test builds: 0 Mario jumping into the first JumpBranch (its row, -2 streaming, -1 none); 1 its row while he swings */
int game_test_jbranch(int phase);
/* Test builds: the first TicoRail's row and rail coordinate (-2: Mario sent there while its zone streams in) */
int game_test_ticorail(float *coord);
/* Test builds: the cursor onto the first live walker of a kind; its row, -2 behind the camera, -1 none */
int game_test_point_walker(int kind);
/* Test builds: the first live walker of a kind as text (row, phase, timer, distance to Mario) */
void game_walker_state(int kind, char *out, int n);
/* Test builds: Mario kept still where he is (set: this place from now on) */
void game_test_hold(int set);
/* Test builds: Mario dropped onto the first standing BeeFlowerHover; its row, -2 streaming, -1 none */
int game_test_beeflower(void);
/* Test builds: Mario at the first unbroken SmallStone ring (spin: spinning); its row or -1 */
int game_test_stone(int spin);
/* Test builds: Mario held half way along the first LavaProminence's rail; its row, -2 streaming, -1 none */
int game_test_prom(void);
/* Test builds: a shot at the first unopened TreasureSpot / CoinFlower (cursor on it, L1): its row, -2 behind the
 * camera, -1 none; _near: Mario held 600 off it */
int game_test_tspot(void);
void game_test_tspot_near(void);
/* Test builds: the cursor onto the first live walking enemy of a kind and L1 (a star piece shot; one given if
 * none); its row, -2 behind the camera, -1 none */
int game_test_shoot(int kind);
/* Test builds: hold: R2 held with the cursor on the first GCaptureTarget within 2400 of Mario (its row, -1 none);
 * else R2 let go, returns the capture phase */
int game_test_gcap(int hold);
/* The star pointer's cursor (right stick), HUD units (x 0..640, y 0..448); set: test builds */
void game_cursor(float *x, float *y);
void game_set_cursor(float x, float y);
/* Test builds: Mario put on a live piece of the first StarPieceFlow; its piece index, -2 all taken, -1 none */
int game_test_flow(void);
/* Test builds: Mario put 1500 in front of the first HomingKiller launcher (-2: at its row while its zone streams
 * in); its row or -1 */
int game_test_killer(void);
/* Test builds: R2 held with the cursor on the first flying HomingKiller bullet (its row, -1 none); 0: let go */
int game_test_freeze(int on);
/* Test builds: R2 with the cursor on the first clinging Karikari (its row, -1 none); 0: let go */
int game_test_kari_point(int on);
/* Test builds: Mario held half way down the first MeteorStrike's rail; its row or -1 */
int game_test_meteor(void);
/* Test builds: Mario put into the first Kiraira (-2: over its row while its zone streams in); its row or -1 */
int game_test_kiraira(void);
/* Test builds: the first Tsukidashikun's row (-1 none) and rail coordinate; drop: Mario put over it */
int game_test_tsuki(int drop, float *coord);
/* Test builds: Mario put into the first WarpPod that has a pair; its row or -1 */
int game_test_warppod(void);
/* Test builds: Mario dropped 600 over the first JumpStand (hip: in a hip drop fall); its row or -1 */
int game_test_jumpstand(int hip);
/* Test builds: Mario put 300 over the k-th AssemblyBlock's place; its row or -1. game_assembled: blocks not at home */
int game_test_assembly(int k);
int game_assembled(void);
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
int game_misses(void);
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
