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
    switch (type) {
    case SEQ_GALAXY_OUT: /* decideNextStageForGalaxyOut */
        if (!strcmp(sq.before, "HeavensDoorGalaxy")) { snprintf(st, sizeof st, "AstroGalaxy"); no = astro_galaxy_no(); sq.start_set = 1; sq.start_id = 100; sq.start_zone = 0; }
        else goto astro_or_dome;
        break;
    case SEQ_STAGE_CLEAR: /* the clear's demo sequence ends with the save (tryStartSave); decideNextEventForClearGalaxy: the grand stars and the story's demos go to the
                           * Observatory; a power star back to its dome. ponytail: the demos themselves not played */
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
    case SEQ_SCENARIO_SELECT: /* requestStartScenarioSelect: the player's choice first; a landed comet: its star decided */
        if (no < 1) { menu_scenario_select(st, gd_comet_star(st)); return; }
        break;
    case SEQ_MISS: stage_of(st, sizeof st, &no); break;
    }
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
