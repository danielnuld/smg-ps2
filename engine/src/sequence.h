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

#endif
