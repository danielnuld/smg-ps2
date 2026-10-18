#ifndef SEQUENCE_H
#define SEQUENCE_H

/* GameSequenceProgress::requestGalaxyMove and StorySequenceExecutor::moveGalaxy: where the game goes next (the next
 * scene "<Stage>/<Scenario>" and Mario's start there). Persistent over scene changes (outside the game modules). */
enum {
    SEQ_MOVE = 0,         /* requestChangeStageInGameMoving / requestGoToAstroGalaxy / ...FromAstroGalaxy */
    SEQ_SCENARIO_SELECT = 2, /* requestStartScenarioSelect: the scenario chosen by the player first */
    SEQ_GALAXY_OUT = 3,   /* requestChangeStageGoBackAstroDome */
    SEQ_STAGE_CLEAR = 4,  /* requestChangeStageAfterStageClear */
    SEQ_MISS = 5,         /* requestChangeStageAfterMiss */
    SEQ_AFTER_LOADING = 6, /* requestChangeStageInGameAfterLoadingGameData */
    SEQ_TITLE = 7         /* requestChangeSceneTitle */
};

void seq_galaxy_move(int type, const char *stage, int scenario, int start_id, int start_zone);
void seq_set_cleared(int star_id);    /* GameSequenceFunction::getClearedPowerStarId: the star the clear brought */
int seq_start_id(int *zone);          /* this scene's start (JMapIdInfo: MarioNo, zone); -1 the default */
const char *seq_before_stage(int *scenario); /* setBeforeStageScenario: the stage left last */
int seq_story_grand_dome(const char *galaxy); /* GameDataConst::getIncludedGrandGalaxyId */
/* the wipe the next scene opens with (openSystemWipe* after a close): frames (0 none) and its colour 0xRRGGBB */
/* GameSequenceFunction's cleared stage (the result informer's): its star, the star counts, coins, star pieces */
typedef struct { char stage[32]; int star, prev_stars, after_stars, coins, prev_max_coin, pieces; } SeqResult;
void seq_set_result(const SeqResult *r);
int seq_take_result(SeqResult *r);
int seq_take_save_prompt(void);  /* after a stage clear the next scene asks to save (tryStartSave) */
void seq_set_open_wipe(int frames, unsigned rgb);
void seq_set_select_after(const char *galaxy);  /* the next scene opens this galaxy's scenario select (FinalBattle's end) */
int seq_take_select_after(char *galaxy, int n);
int seq_take_open_wipe(unsigned *rgb);
/* StorySequenceExecutor's demo sequences queued by the last move (game.c story_update plays them) */
typedef struct { unsigned char type, ev; const char *name; } SeqDemo;
const SeqDemo *seq_demo_current(void); /* getCurrentDemoInfo, NULL none */
void seq_demo_step(void);              /* the current list's next info */
int seq_demo_next(void);               /* tryNextDemoInfo: the next list, 0 none */
void seq_demo_clear(void);
int seq_demo_saved(int set);           /* _B0: the save ran in the sequence */
int seq_demo_event(void);              /* getExecutingEventEnum */

#endif
