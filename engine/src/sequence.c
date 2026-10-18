#include "sequence.h"

#include "game.h"
#include "gamedata.h"
#include "menu.h"

#include <stdio.h>
#include <string.h>

static struct {
    char before[32];  /* StorySequenceExecutor mStageName / mScenarioNo */
    int before_no;
    int cleared;      /* the star id the last stage clear gave */
    int start_id, start_zone, start_set;
    int open_frames;  /* the next scene's opening wipe */
    int save_prompt;  /* a stage clear: the next scene asks to save */
    unsigned open_rgb;
} sq;

static SeqResult result;
static int result_set;
void seq_set_result(const SeqResult *r) { result = *r; result_set = 1; }
int seq_take_result(SeqResult *r) { if (!result_set) return 0; *r = result; result_set = 0; return 1; }
static char select_after[32];
void seq_set_select_after(const char *galaxy) { snprintf(select_after, sizeof select_after, "%s", galaxy); }
int seq_take_select_after(char *galaxy, int n) { snprintf(galaxy, n, "%s", select_after); int r = select_after[0] != 0; select_after[0] = 0; return r; }
int seq_take_save_prompt(void) { int r = sq.save_prompt; sq.save_prompt = 0; return r; }
void seq_set_open_wipe(int frames, unsigned rgb) { sq.open_frames = frames; sq.open_rgb = rgb; }
int seq_take_open_wipe(unsigned *rgb) { int f = sq.open_frames; *rgb = sq.open_rgb; sq.open_frames = 0; return f; }

int seq_start_id(int *zone)
{
    if (zone) *zone = sq.start_zone;
    return sq.start_set ? sq.start_id : -1;
}
const char *seq_before_stage(int *scenario) { if (scenario) *scenario = sq.before_no; return sq.before; }
void seq_set_cleared(int star_id) { sq.cleared = star_id; }
int seq_story_grand_dome(const char *galaxy) { return gd_grand_galaxy_no(galaxy); }

/* --- StorySequenceExecutor's demo sequences (StorySequenceExecutor.cpp): lists of {type, event, name}, each ending in
 * 12 (then idle) or 13 (then the save and a fade in); types 0 a time-keep demo by name, 1 a fade in, 2 a movie, 3 a
 * scene change, 4 the title, 5 the save, 6 an event flag on, 7 a story event followed, 8 the prologue, 9 the staff
 * roll, 10 / 11 system settings. The lists queued by moveGalaxy run once the next scene starts (game.c story_update). */
static const SeqDemo cDemoPowerStarResultAstroDome[] = {{0, 1, "天文ドームスター帰還"}, {13, 0, ""}};
static const SeqDemo cDemoPowerStarResultAstroDomeWithSave[] = {{0, 1, "天文ドームスター帰還"}, {5, 0, "Save"}, {12, 0, ""}};
static const SeqDemo cDemoPowerStarResultAstroGalaxy[] = {{0, 1, "パワースター帰還"}, {13, 0, ""}};
static const SeqDemo cDemoGrandStarResult[] = {{0, 1, "グランドスター帰還[２回目以降]"}, {13, 0, ""}};
static const SeqDemo cDemoGrandStar1ResultFirst[] = {{0, 1, "グランドスター１帰還"}, {0, 1, "ロゼッタ状況説明デモ"}, {13, 0, ""}};
static const SeqDemo cDemoTorchLecture[] = {{0, 1, "グランドスター２帰還"}, {0, 1, "ロゼッタトーチの炎説明デモ"},
                                            {0, 1, "バトラーマップレクチャー"}, {13, 0, ""}};
static const SeqDemo cDemoRosettaTalkAboutKoopa[] = {{0, 1, "グランドスター３帰還"}, {0, 1, "ロゼッタ銀河の中心説明デモ"}, {13, 0, ""}};
static const SeqDemo cDemoRosettaTalkAstroDomeRecover[] = {{0, 1, "グランドスター４帰還"}, {0, 1, "ロゼッタ天文台機能回復デモ"}, {13, 0, ""}};
static const SeqDemo cDemoGrandStarReturn5[] = {{0, 1, "グランドスター５帰還"}, {13, 0, ""}};
static const SeqDemo cDemoRosettaTalkCountDownStart[] = {{0, 1, "ロゼッタカウントダウン開始デモ"}, {13, 0, ""}};
static const SeqDemo cDemoGrandStarReturn6[] = {{0, 1, "グランドスター６帰還"}, {13, 0, ""}};
static const SeqDemo cDemoRosettaTalkTorchProgress[] = {{0, 1, "ロゼッタトーチの炎進捗デモ"}, {13, 0, ""}};
static const SeqDemo cDemoRosettaTalkKoopaBattleVs3GalaxyAppear[] = {{5, 0, "Save"}, {0, 1, "ロゼッタ最終決戦デモ"}, {12, 0, ""}};
static const SeqDemo cDemoStarPieceLectureRetry[] = {{0, 3, "スターピース解説前半"}, {1, 1, "Fadein"}, {12, 0, ""}};
static const SeqDemo cDemoNewGalaxyDiscover[] = {{0, 1, "天文ドームスター帰還"}, {0, 4, "バトラー報告"}, {13, 0, ""}};
static const SeqDemo cDemoAstroDomeLecture[] = {{0, 2, "ドームレクチャー１"}, {7, 0, "バトラー情報Ａ"}, {12, 0, ""}};
static const SeqDemo cDemoRosettaTalkAfterNormalEnding[] = {{0, 1, "ロゼッタノーマルエンディング後デモ"},
                                                            {6, 1, "RosettaTalkAfterNormalEnding"}, {1, 1, "Fadein"}, {12, 0, ""}};
/* cDemoFortressDiscoverCheckListTable: a fortress galaxy that can open, its story event, a Koopa one (5 / 6) */
static const struct { const char *stage, *event; int koopa; } cFortress[] = {
    {"TriLegLv1Galaxy", "クッパＪｒロボプラント発見", 0}, {"KoopaBattleVs1Galaxy", "クッパスタープラント発見", 1},
    {"KoopaJrShipLv1Galaxy", "クッパＪｒシッププラント発見", 0}, {"KoopaBattleVs2Galaxy", "クッパダークマタープラント発見", 1},
    {"FloaterOtaKingGalaxy", "クッパＪｒクリーチャープラント発見", 0}};

#define SEQ_MAX_DEMOS 8
static const SeqDemo *sdemo[SEQ_MAX_DEMOS]; /* _48: each list's current info */
static int nsdemo;
static SeqDemo sdyn[8];                    /* _6C: addDynamicDemoSequenceInfo */
static int nsdyn;
static int sdemo_save;                     /* _B0: the save ran */

static void prepare_demo(const SeqDemo *d) { if (nsdemo < SEQ_MAX_DEMOS) sdemo[nsdemo++] = d; } /* prepareDemoSequence */
const SeqDemo *seq_demo_current(void) { return nsdemo ? sdemo[0] : NULL; }  /* getCurrentDemoInfo */
void seq_demo_step(void) { if (nsdemo) sdemo[0]++; }                       /* _48[0]++ */
int seq_demo_next(void) /* tryNextDemoInfo */
{
    if (nsdemo <= 1) return 0;
    memmove(sdemo, sdemo + 1, (nsdemo - 1) * sizeof sdemo[0]);
    nsdemo--;
    return 1;
}
void seq_demo_clear(void) { nsdemo = 0; nsdyn = 0; sdemo_save = 0; } /* _48.clear(), _B0 = false */
int seq_demo_saved(int set) { if (set) sdemo_save = 1; return sdemo_save; }
int seq_demo_event(void) { return nsdemo ? sdemo[0]->ev : 0; } /* getExecutingEventEnum */

static const SeqDemo *add_dyn(int type, int ev, const char *name) /* addDynamicDemoSequenceInfo */
{
    SeqDemo *d = &sdyn[nsdyn < 7 ? nsdyn++ : 7];
    d->type = (unsigned char)type; d->ev = (unsigned char)ev; d->name = name;
    return d;
}

/* decideNextEventForMoveGalaxy */
static void decide_move_galaxy(const char *st, int no, int clear)
{
    if (!strcmp(st, "AstroDome")) {
        if (!gd_story_passed("バトラー情報Ａ")) { prepare_demo(cDemoAstroDomeLecture); return; }
        if (gd_has_star("EggStarGalaxy", 1) && !gd_story_passed("スターピースレクチャー")) { prepare_demo(cDemoStarPieceLectureRetry); return; }
        for (unsigned i = 0; i < sizeof cFortress / sizeof cFortress[0]; i++) { /* prepareDemoSequenceButlerFortressDiscover */
            if (!gd_flag_can_on(cFortress[i].stage) || gd_flag_is_on(cFortress[i].stage)) continue; /* canOnAndIsOffGameEventFlag */
            if (gd_story_passed(cFortress[i].event) || no != gd_grand_galaxy_no(cFortress[i].stage)) continue;
            const SeqDemo *d = add_dyn(0, cFortress[i].koopa ? 6 : 5, "バトラー報告");
            add_dyn(7, 0, cFortress[i].event);
            if (clear) add_dyn(13, 0, "");
            else { add_dyn(1, 1, "Fadein"); add_dyn(12, 0, ""); }
            prepare_demo(d);
        }
    }
    /* ponytail: LibraryRoom's picture book (cDemoRosettaPicureBookTalk) waits for the picture book chapters */
}

/* decideNextEventForClearGalaxy's demos (the stage is chosen in seq_galaxy_move). ponytail: isOnJustGameEventFlag
 * (newly on) is not kept, so its lists wait (cDemoKinopioRescued, cDemoTrickCometLecture, cDemoLuigiRescued,
 * cDemoGreenStarGetFirst / Release), no Luigi (cDemoLuigiMissing), no picture book (cDemoRosettaPicureBookTalk) */
static void decide_clear_galaxy(const char *st)
{
    const char *b = sq.before;
    int bn = sq.before_no;
    if (!strcmp(b, "KoopaBattleVs3Galaxy")) return; /* cDemoEpilogue*: the movies (game_request_movie) */
    if (!result_set || result.prev_stars != result.after_stars) { /* !hasPowerStarYetAtResultSequence */
        int n0 = nsdemo;
        if (!strcmp(b, "HeavensDoorGalaxy") && bn == 1) prepare_demo(cDemoGrandStar1ResultFirst);
        if (!strcmp(b, "EggStarGalaxy") && bn == 1) prepare_demo(cDemoPowerStarResultAstroDomeWithSave);
        if (!strcmp(b, "EggStarGalaxy") && bn == 2) prepare_demo(cDemoNewGalaxyDiscover);
        if (!strcmp(b, "TriLegLv1Galaxy") && bn == 1) prepare_demo(cDemoTorchLecture);
        if (!strcmp(b, "KoopaBattleVs1Galaxy") && bn == 1) prepare_demo(cDemoRosettaTalkAboutKoopa);
        if (!strcmp(b, "KoopaJrShipLv1Galaxy") && bn == 1) prepare_demo(cDemoRosettaTalkAstroDomeRecover);
        if (!strcmp(b, "KoopaBattleVs2Galaxy") && bn == 1) { prepare_demo(cDemoGrandStarReturn5); prepare_demo(cDemoRosettaTalkCountDownStart); }
        if (!strcmp(b, "FloaterOtaKingGalaxy") && bn == 1) {
            prepare_demo(cDemoGrandStarReturn6);
            if (!gd_flag_can_on("KoopaBattleVs3Galaxy")) prepare_demo(cDemoRosettaTalkTorchProgress);
        }
        if (nsdemo > n0) {
            int need = gd_stars_to_open("KoopaBattleVs3Galaxy"); /* getOptionalDemoForClearGalaxy: canOnJustGameEventFlag */
            if (result_set && result.prev_stars < need && result.after_stars >= need) prepare_demo(cDemoRosettaTalkKoopaBattleVs3GalaxyAppear);
            return;
        }
    }
    if (gd_is_grand_star(b, bn)) prepare_demo(cDemoGrandStarResult);
    else if (!strcmp(st, "AstroDome")) prepare_demo(cDemoPowerStarResultAstroDome);
    else prepare_demo(cDemoPowerStarResultAstroGalaxy);
}

static void stage_of(char *out, int n, int *scenario) /* MR::getCurrentStageName / getCurrentScenarioNo */
{
    const char *sl = strchr(game_stage_name, '/');
    snprintf(out, n, "%.*s", sl ? (int)(sl - game_stage_name) : (int)strlen(game_stage_name), game_stage_name);
    if (scenario) *scenario = sl ? sl[1] - '0' : 1;
}

/* calcAproposScenarioNoOnAstroGalaxy: the Observatory's state by the grand stars */
static int astro_galaxy_no(void)
{
    if (gd_flag_can_on("KoopaBattleVs3Galaxy")) return 5;
    if (gd_flag_is_on("SpecialStarGrand5")) return 3;
    if (gd_flag_is_on("SpecialStarGrand3")) return 2;
    if (gd_flag_is_on("SpecialStarGrand1")) return 4; /* isOnJustGameEventFlag. ponytail: "just" (newly on) not kept */
    return 1;
}
static int astro_dome_no(void) { return gd_grand_galaxy_no(sq.before); } /* calcAproposScenarioNoOnAstroDome */

void seq_galaxy_move(int type, const char *stage, int scenario, int start_id, int start_zone)
{
    char st[32];
    int no = scenario;
    snprintf(st, sizeof st, "%s", stage ? stage : "");
    /* setBeforeStageScenario: the stage being left (a clear: the cleared stage and its star) */
    int cur_no;
    stage_of(sq.before, sizeof sq.before, &cur_no);
    sq.before_no = type == SEQ_STAGE_CLEAR ? sq.cleared : cur_no;
    sq.start_set = start_id >= 0; sq.start_id = start_id; sq.start_zone = start_zone;
    if (!strcmp(st, "AstroGalaxy") && no != 4) no = astro_galaxy_no();
    if (!strcmp(st, "AstroDome") && no == -1) no = astro_dome_no();
    seq_demo_clear();
    switch (type) {
    case SEQ_GALAXY_OUT: /* decideNextStageForGalaxyOut */
        if (!strcmp(sq.before, "HeavensDoorGalaxy")) { snprintf(st, sizeof st, "AstroGalaxy"); no = astro_galaxy_no(); sq.start_set = 1; sq.start_id = 100; sq.start_zone = 0; }
        else goto astro_or_dome;
        break;
    case SEQ_STAGE_CLEAR: /* the clear's demo sequence ends with the save (tryStartSave); decideNextEventForClearGalaxy: the grand stars and the story's demos go to the
                           * Observatory; a power star back to its dome (decide_clear_galaxy queues the demos) */
        if (!strcmp(sq.before, "HeavensDoorGalaxy") || gd_is_grand_star(sq.before, sq.before_no) ||
            !strcmp(sq.before, "TriLegLv1Galaxy") || !strcmp(sq.before, "KoopaBattleVs1Galaxy") ||
            !strcmp(sq.before, "KoopaJrShipLv1Galaxy") || !strcmp(sq.before, "KoopaBattleVs2Galaxy") ||
            !strcmp(sq.before, "FloaterOtaKingGalaxy")) {
            snprintf(st, sizeof st, "AstroGalaxy"); no = astro_galaxy_no();
            break;
        }
        if (!strcmp(sq.before, "KoopaBattleVs3Galaxy")) { /* cDemoEpilogueNormal / cDemoEpilogueComplete, then the title.
            * ponytail: EpilogueDemoStage's "エピローグデモ" and the staff roll not played (the movies only); complete
            * as 120 stars */
            game_request_movie(gd_star_num() >= 120 ? "EpilogueA,EndingA,EndingB" : "EpilogueA,EndingA");
            snprintf(st, sizeof st, "FileSelect"); no = 1; type = SEQ_TITLE;
            break;
        }
    astro_or_dome: /* setNextStageToAstroGalaxyOrDome */
        no = astro_dome_no();
        if (no >= 1 && no <= 6) snprintf(st, sizeof st, "AstroDome");
        else { snprintf(st, sizeof st, "AstroGalaxy"); no = astro_galaxy_no(); }
        break;
    case SEQ_AFTER_LOADING: /* overwriteGalaxyNameAfterLoading. ponytail: no prologue movie */
        if (gd_flag_is_on("SpecialStarGrand1")) { snprintf(st, sizeof st, "AstroGalaxy"); no = astro_galaxy_no(); }
        else if (gd_story_passed("ピーチ城浮上後")) { snprintf(st, sizeof st, "HeavensDoorGalaxy"); no = 1; }
        else if (gd_story_passed("クッパ襲来後")) { snprintf(st, sizeof st, "PeachCastleGardenGalaxy"); no = 1; sq.start_set = 1; sq.start_id = 1; sq.start_zone = 0; }
        else { snprintf(st, sizeof st, "PeachCastleGardenGalaxy"); no = 1; }
        break;
    case SEQ_TITLE: snprintf(st, sizeof st, "FileSelect"); no = 1; break;
    case SEQ_MOVE: decide_move_galaxy(st, no, 0); break;
    case SEQ_SCENARIO_SELECT: /* requestStartScenarioSelect: the player's choice first; a landed comet: its star decided */
        if (no < 1) { menu_scenario_select(st, gd_comet_star(st)); return; }
        break;
    case SEQ_MISS: stage_of(st, sizeof st, &no); break;
    }
    if (type == SEQ_STAGE_CLEAR) { decide_clear_galaxy(st); decide_move_galaxy(st, no, 1); }
    if (type == SEQ_AFTER_LOADING && gd_flag_can_on("RosettaTalkAfterNormalEnding") && !gd_flag_is_on("RosettaTalkAfterNormalEnding"))
        prepare_demo(cDemoRosettaTalkAfterNormalEnding); /* decideNextEventForAfterLoading */
    if (type == SEQ_STAGE_CLEAR) { /* updateGameDataAndSequenceAfterStageResultSequence */
        sq.save_prompt = 1;
        gd_comet_sync();
        gd_comet_next();
    }
    gd_comet_sync(); /* GameSequenceProgress: syncWithFlags at every move */
    if (!st[0] || no < 1) return;
    char next[48];
    snprintf(next, sizeof next, "%s/%d", st, no);
    printf("sequence: move %d -> %s (start %d)\n", type, next, sq.start_set ? sq.start_id : -1);
    game_request_scene(next);
}
