#ifndef MENU_H
#define MENU_H

#include "blob.h"

/* Screens outside the game's action (Petari src/Game/Screen): the pause menu and its confirm window so far.
 * game.c calls menu_update at the start of each logic step (1: the game is paused, its world stops) and menu_draw
 * last in the frame. */
void menu_init(const Scene *s);
/* MarioSubMeter: 0 frameOut (ground), 1 water, 2 bee; its ratio; each frame (Mario resting, his screen place) */
void menu_submeter_mode(int mode);
void menu_submeter_ratio(int bee, float ratio);
void menu_submeter_update(int resting, float x, float y, int visible);
int menu_update(void);
void menu_draw(void);
int menu_paused(void);
/* a fade to black over `frames`, then seq_galaxy_move (closeSystemWipe*, then the request) */
void menu_fade_move(int frames, int type, const char *stage, int scenario, int start_id, int start_zone);
int menu_fading(void);
void menu_fade_hold(int frames);
void menu_fade_open(int frames);
/* ScenarioSelectScene: the galaxy's scenario chosen over the frozen stage, then the move there (sequence.c) */
void menu_scenario_select(const char *galaxy, int decided); /* decided: the scenario fixed (a comet's), 0 the choice */
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
/* the demos' (game.c DemoExecutor): SceneWipeHolder's fades and CinemaFrame */
void menu_wipe(int close, int frames, u32 rgb); /* WipeFade: closes / opens over frames (< 0: sFadeFrame 30), rgb 0xBBGGRR */
void menu_wipe_force(int close);     /* forceOpenWipe / forceCloseWipe (the colour kept) */
int menu_wipe_closed(void);          /* !isWipeOpen: closed and still */
void menu_cinema(int frame);         /* CinemaFrame: tryScreenToFrame (1) / tryFrameToScreen (0) */
void menu_monologue_start(void);     /* RosettaMonologue (ロゼッタ状況説明デモ's picture book): appear */
int menu_monologue_alive(void);      /* !isDead */
/* GalaxyMapController's start modes (its Mode): MR::startAstroMapLayout, startGalaxyMapLayout,
 * startAstroMapLayoutForNewGalaxyDiscover / ForNewTicoGalaxyDiscover / ForNewDomeDiscover,
 * startGalaxyMapLayoutForTicoComet, startAstroMapLayoutForChallengeGalaxyDiscover */
enum { GMAP_ASTRO, GMAP_GALAXY, GMAP_LIST, GMAP_NEW_GALAXY, GMAP_NEW_TICO, GMAP_NEW_DOME, GMAP_TICO_COMET, GMAP_CHALLENGE };
void menu_galaxy_map(int mode);
void menu_luigi_letter(const char *galaxy, int talk); /* LuigiLetter (LuigiPicture<galaxy>): appear (talk: KinopioAstro's) */
int menu_luigi_letter_alive(void);   /* !isDead */
int menu_test_pml(void);            /* tests: the pause menu letter button: made 10, shown 1 */
int menu_test_letter(void);         /* tests: 10 x letter on + its IconAButton nerve + 1 (0 dead) */
int menu_galaxy_map_active(void);
int menu_test_point_map(const char *pane);
int menu_file_selecting(void);       /* the FileSelect stage's FileSelector running */
void menu_save_request(void);        /* the files changed: the save is to be written */
void menu_save_start(int confirm, int remind, int quit); /* SaveDataHandleSequence::startSave */
int menu_saving(void);
int menu_save_result(void);          /* 2 saved, 3 not */
int menu_result_showing(void);       /* StageResultInformer */
void menu_staffroll(int ev);         /* StaffRoll: 12 appear (to its pause), 13 startInfo (the infos, the save, End) */
int menu_staffroll_pause_or_end(void); /* StaffRoll::isPauseOrEnd */
int menu_staffroll_on(void);         /* the staff roll over the screen (the scene not drawn, Mario waits) */
void menu_result_start(void);        /* the result held for the star return demo */
/* the star pointer (StarPointerLayout): a StarPointerModeEnum asked for this logic step (MR::startStarPointerMode* /
 * requestStarPointerMode*: the lowest asked is set up), a touch (the pointing checks' onReaction with enableTouch;
 * rumble: their "弱"; 0 when the pointer is not valid or not in the screen), SpaceCocoon's command stream on / off
 * (each step), the layout drawn over the frame (main.c) */
enum { SPM_SAVELOAD = 2, SPM_TITLE = 4, SPM_FILESELECT, SPM_SCENARIOSELECT, SPM_PAUSEMENU, SPM_CHOOSEYESNO, SPM_DEMO = 12,
       SPM_DEMOMARIODEATH, SPM_POWERSTARGETDEMO, SPM_SPHEREONREACTION = 16, SPM_SPHEREFINGER, SPM_COMMANDSTREAM = 19,
       SPM_BLUESTAR, SPM_BLUESTARREADY, SPM_GAME = 25 };
void menu_pointer_request(int mode);
int menu_pointer_touch(const void *id, int rumble);
void menu_pointer_stream(int on);
void menu_pointer_draw(void);
int menu_guidance_request(const char *label, int timeout); /* StarPointerGuidance::request1PGuidance (each frame), 0 none */
int menu_guidance_exists(void);      /* MR::isExistStarPointerGuidance */
/* TimeLimitLayout (a Red / Black comet's): appear with the limit (frames), a frame on, kill; isReadyToTimeUp; frames left */
void menu_time_limit_appear(int frames);
void menu_time_limit_update(void);
void menu_time_limit_kill(void);
int menu_time_limit_up(void);
int menu_time_limit_left(void);        /* -1 not shown */
/* YesNoLayout: MR::appearYesNoSelector(yes, no) (talk_se: TalkDirector's setYesNoSelectorSE), isYesNoSelected */
void menu_yesno_appear(const char *yes, const char *no, int talk_se);
int menu_yesno_selected(void);       /* 1 Yes, 2 No, 0 not yet */
int menu_take_save_request(void);    /* main.c: 1 once per request */
int menu_draw_screen(void); /* game_draw: 1 when a whole-screen menu (the scenario select) drew the frame */
const u16 *msg_get(const char *label); /* MR::getLayoutMessageDirect / getGameMessageDirect (MSGS chunk), NULL none */
void msg_set(const u8 *msgs_chunk);    /* msg_get's chunk (menu_init's; main.c's subs.bin while a movie plays) */
int menu_test_point_scenario(const char *pane);
int menu_test_point_dome(const char *pane);
int menu_test_point_file(int slot, const char *pane);
int menu_test_point(const char *pane); /* tests: the pointer on a pane of the open pause menu / its window */

/* game.c */
int game_pause_permitted(void);     /* GameScene::isPermitToPauseMenu */
int game_story_busy(void);          /* StorySequenceExecutor playing its demo sequence (the save waits) */
void game_cursor_menu(void);        /* the pointer moved by the right stick and shown (startStarPointerModePauseMenu) */
int game_pointer_a(void);           /* the pointer's A held (testCorePadButtonA: R2, or X, the Wii's A also) */
int game_scenario_stars(int n);     /* hasPowerStarInCurrentStage(n) */

#endif
