#ifndef GAMEDATA_H
#define GAMEDATA_H

#include "blob.h"

/* The save file's game data (Petari GameDataHolder: GameDataAllGalaxyStorage, GameEventFlagChecker /
 * GameEventFlagTable, GameEventValueChecker, GameDataPlayerStatus, StarPieceAlmsStorage) and the tables it reads
 * (menus.bin GDAT: cStoryProgressTable, cGalaxyInfoTable, every galaxy's ScenarioData). It lives outside the game
 * modules' sections: it stays over scene changes. */
void gd_attach(const Scene *s);                 /* each scene: its GDAT (the first one also resets the data) */
void gd_reset(void);                            /* GameDataHolder::resetAllData */

int gd_scenario_num(const char *galaxy);        /* GalaxyStatusAccessor::getScenarioNum */
int gd_power_star_num(const char *galaxy);      /* getPowerStarNum: its scenarios with a star */
int gd_scenario_star_mask(const char *galaxy, int scenario); /* PowerStarId (a mask of its stars) */
const char *gd_scenario_comet(const char *galaxy, int scenario); /* Comet ("" none) */
int gd_scenario_hidden(const char *galaxy, int scenario);    /* IsHidden */

int gd_has_star(const char *galaxy, int star);  /* GameDataHolder::hasPowerStar (star 1..) */
void gd_set_star(const char *galaxy, int star);
int gd_star_num(void);                          /* calcCurrentPowerStarNum */
int gd_star_num_galaxy(const char *galaxy);     /* getPowerStarNumOwned */
int gd_visited(const char *galaxy, int scenario);
int gd_visited_any(const char *galaxy);          /* isAlreadyVisitedStage */
int gd_stars_to_open(const char *galaxy);        /* GameDataConst::getPowerStarNumToOpenGalaxy */
void gd_set_visited(const char *galaxy, int scenario);
void gd_update_max_coins(const char *galaxy, int scenario, int coins);
int gd_max_coins(const char *galaxy, int scenario);

int gd_flag_known(const char *name);            /* a flag of the table */
int gd_galaxy_count(void);                      /* cGalaxyInfoTable's rows */
void gd_galaxy_at(int i, const char **name, const char **map_pane);
int gd_flag_can_on(const char *name);           /* GameEventFlagChecker */
int gd_flag_is_on(const char *name);
int gd_tico_galaxy(int i, const char **g, int *given, int *max); /* the i-th hungry Luma's galaxy, star pieces given / needed */
const char *gd_flag_galaxy(const char *name);    /* GameEventFlagAccessor::getGalaxyName (a special star's galaxy), NULL none */
void gd_scene_start(void);                       /* the scene start's game data (getSceneStartGameDataHolder) */
int gd_flag_is_on_just(const char *name);        /* isOnJustGameEventFlag: on now, off as the scene started */
int gd_flag_can_on_just(const char *name);       /* canOnJustGameEventFlag */
int gd_green_first_just(void);                   /* StageResultSequenceChecker::isJustGetGreenStarFirst */
int gd_galaxy_appear_just(const char *galaxy);   /* GameDataFunction::isOnJustAppearGalaxy */
int gd_just_tico_galaxy(void);                   /* StageResultSequenceChecker::isJustAppearTicoGalaxy */
int gd_just_open_galaxy(void);                   /* isJustOpenGalaxyWithoutChallengeGalaxy */
int gd_is_special_star(const char *galaxy, int star, const char *kind); /* GameDataConst::isPowerStarSpecial */
int gd_question_galaxy(const char *galaxy);      /* GameDataConst::isQuestionGalaxy */
int gd_flag_try_on(const char *name);
int gd_grand_galaxy_no(const char *galaxy);     /* GameDataConst::getIncludedGrandGalaxyId (its dome) */
int gd_is_grand_star(const char *galaxy, int star); /* GameDataConst::isGrandStar */
int gd_galaxy_open(const char *galaxy);         /* GalaxyStatusAccessor::isOpened (no stars: open) */
int gd_galaxy_appear(const char *galaxy);       /* GameDataHolder::isAppearGalaxy */
int gd_story_passed(const char *event);         /* isPassedStoryEvent */
void gd_story_follow(const char *event);        /* followStoryEventByName */
int gd_value(const char *name);                 /* GameEventValueChecker */
void gd_set_value(const char *name, int v);

int gd_player_left(void);
void gd_add_player_left(int n);
int gd_stocked_star_pieces(void);
void gd_add_stocked_star_pieces(int n);

void gd_comet_sync(void);            /* GalaxyCometScheduler::syncWithFlags */
void gd_comet_next(void);            /* forceToNextState (after a stage result) */
int gd_comet_star(const char *galaxy); /* the landed comet's scenario (4 normal, 5 purple coins), 0 none */

/* Luigi (GameDataConst / FindingLuigiEventScheduler / EventUtil): his stars, the hide-and-seek state, the letter */
int gd_star_luigi_has(const char *galaxy, int star);         /* GameDataConst::isPowerStarLuigiHas */
int gd_galaxy_luigi_arrested(const char *galaxy, int star);  /* isGalaxyLuigiArrested (-1 any star) */
void gd_luigi_stage_result(const char *stage, int star, int has_yet); /* updateOnStageResult */
void gd_luigi_update(int move_type);                         /* update(GalaxyMoveArgument) */
int gd_any_left_supply(void);      /* MR::isAnyPlayerLeftSupply (the messenger's Peach letter) */
int gd_luigi_left_supply(void);    /* MR::isLuigiLeftSupply */
void gd_off_all_left_supply(void); /* MR::offAllPlayerLeftSupply */
void gd_add_miss_point(int n);     /* addMissPoint (decPlayerLeft 1, incPlayerGameOverNum 3) */
void gd_luigi_set_hiding(void);                              /* onGameEventFlagGetLuigiLetter: setStateHiding */
void gd_luigi_hiding_galaxy(const char **galaxy, int *star); /* getLuigiHidingGalaxyNameAndStarId */
int gd_luigi_disappear(void);                                /* isLuigiDisappearFromAstroGalaxy */
int gd_luigi_hiding_any(void);                               /* isLuigiHidingAnyGalaxy */
int gd_luigi_active(void);                                   /* isActiveLuigiHideAndSeekEvent */
int gd_luigi_end(void);                                      /* isEndLuigiHideAndSeekEvent */
int gd_luigi_hiding(const char *stage, int scenario);        /* MR::isOnLuigiHiding (scenario -1: none selected) */
int gd_luigi_hiding_current(const char *stage, int scenario); /* isOnLuigiHidingCurrentStage */
int gd_luigi_gone_or_hiding(const char *stage, int scenario); /* isLuigiDisappearFromAstroGalaxyOrHiding */
int gd_luigi_letter_at_messenger(const char *stage, int scenario); /* isLuigiLetterArrivalAtMessenger */
int gd_luigi_hiding_galaxy_scenario(const char *galaxy, int scenario); /* isLuigiHidingGalaxyAndScenario */
const char *gd_luigi_letter_galaxy(const char *stage, int scenario);     /* getLuigiLetterGalaxyName, NULL none */
const char *gd_luigi_letter_galaxy_npc(const char *stage, int scenario); /* getLuigiLetterGalaxyNameForNPC */

/* UserFiles 0..5 (FileSelector's 1..6): created, icon (1 Mario, 2 Luigi, 3 Yoshi, 4 Kinopio, 5 Peach), modified count;
 * each holds Mario's and Luigi's game data, the player last loaded picks one (gd_file_luigi) */
int gd_player_luigi(void);        /* MR::isPlayerLuigi: the loaded data is Luigi's */
int gd_file_luigi(int i);         /* FileSelector::isUserFileLuigi */
void gd_file_set_luigi(int i, int luigi); /* setUserFileMario(!luigi): the data loaded and shown next */
int gd_file_appear_luigi(int i);  /* isUserFileAppearLuigi: Luigi's, or Mario's complete ending seen */
int gd_file_created(int i);
int gd_file_icon(int i);
u32 gd_file_modified(int i);
void gd_file_create(int i, int icon);
void gd_file_delete(int i);
void gd_file_copy(int dst, int src);
void gd_file_set_icon(int i, int icon);
int gd_file_load(int i);          /* the file becomes the current game data */
void gd_file_save(void);          /* the current game data into its file */
int gd_file_current(void);        /* -1 none loaded */
void gd_file_info(int i, int *stars, int *pieces, int *normal_end, int *complete_end);
u8 *gd_image(int *size);          /* the six files as one image (the memory card's save) */
void gd_image_loaded(void);       /* after the image was read back */
/* the file as bytes (the memory card's save) and back; 0 when the bytes are not a file */
int gd_serialize(u8 *buf, int size);
int gd_deserialize(const u8 *buf, int size);

#endif
