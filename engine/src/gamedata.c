#include "gamedata.h"

#include <stdio.h>
#include <string.h>

enum {
    GT_NONE, GT_STARNUM, GT_GALAXYOPENSTAR, GT_SPECIALSTAR, GT_EVENTFLAG, GT_STORYEVENT, GT_GALAXY, GT_COMET, GT_STARPIECE,
    GT_EVENTVALUEISZERO, GT_COMPLETEMARIOANDLUIGI, GT_STORYEVENTSYNC
};
typedef struct { const char *name; u8 type, save, c1, c2; const char *s1, *s2; } GdFlag;
typedef struct { const char *name; u16 init; } GdValue;
#include "gamedata_tab.h"
#define NFLAGS ((int)(sizeof gd_flags / sizeof gd_flags[0]))
#define NVALUES ((int)(sizeof gd_values / sizeof gd_values[0]))

/* menus.bin GDAT (tools/lyt.py gdat_chunk) */
typedef struct { char name[64]; u32 progress; } GStory;
typedef struct { char name[32]; u8 open_stars, grand, pad[2]; char cond[3][40]; char pane[16]; } GInfo;
typedef struct { u8 star_mask, hidden, pad[2]; char comet[12]; u32 luigi; } GScen1;
typedef struct { char galaxy[32]; u8 n, pad[3]; GScen1 s[8]; } GScen;
static const GStory *story;
static const GInfo *info;
static const GScen *scen;
static int nstory, ninfo, nscen;

#define MAX_GAL 64
static struct {
    int ok;
    struct { char name[32]; u8 owned, visited; u16 max_coin[8]; } gal[MAX_GAL]; /* GameDataSomeGalaxyStorage */
    int ngal;
    u8 flag[(NFLAGS + 7) / 8];  /* GameEventFlagStorage */
    u16 value[NVALUES];         /* GameEventValueChecker */
    u8 story;                   /* GameDataPlayerStatus mStoryProgress */
    u32 pieces;                 /* mStockedStarPiece */
    u16 left, left_supply;      /* mPlayerLeft, mPlayerLeftSupply */
    u16 alms[16];               /* StarPieceAlmsStorage */
    u8 complete_ml;             /* UserFile isOnCompleteEndingMarioAndLuigi */
    u8 comet_pos[6], comet_land[6], comet_ready[6], comet_started; /* GalaxyCometScheduler (gd_comet_*) */
} gd;

static const GScen *find_scen(const char *g)
{
    for (int i = 0; i < nscen; i++)
        if (!strcmp(scen[i].galaxy, g)) return &scen[i];
    return NULL;
}
static const GInfo *find_info(const char *g)
{
    for (int i = 0; i < ninfo; i++)
        if (!strcmp(info[i].name, g)) return &info[i];
    return NULL;
}
static int find_gal(const char *g)
{
    for (int i = 0; i < gd.ngal; i++)
        if (!strcmp(gd.gal[i].name, g)) return i;
    return -1;
}
static int find_flag(const char *name)
{
    for (int i = 0; name && i < NFLAGS; i++)
        if (!strcmp(gd_flags[i].name, name)) return i;
    return -1;
}

int gd_scenario_num(const char *g) { const GScen *s = find_scen(g); return s ? s->n : 0; }
int gd_power_star_num(const char *g)
{
    const GScen *s = find_scen(g);
    int n = 0;
    for (int i = 0; s && i < s->n && i < 8; i++) n += s->s[i].star_mask != 0;
    return n;
}
int gd_scenario_star_mask(const char *g, int sc) { const GScen *s = find_scen(g); return s && sc >= 1 && sc <= s->n ? s->s[sc - 1].star_mask : 0; }
const char *gd_scenario_comet(const char *g, int sc) { const GScen *s = find_scen(g); return s && sc >= 1 && sc <= s->n ? s->s[sc - 1].comet : ""; }
int gd_scenario_hidden(const char *g, int sc) { const GScen *s = find_scen(g); return s && sc >= 1 && sc <= s->n ? s->s[sc - 1].hidden : 0; }

/* GameDataAllGalaxyStorage: every galaxy with stars (ScenarioDataIter) */
void gd_reset(void)
{
    memset(&gd, 0, sizeof gd);
    for (int i = 0; i < nscen && gd.ngal < MAX_GAL; i++)
        if (gd_power_star_num(scen[i].galaxy)) snprintf(gd.gal[gd.ngal++].name, 32, "%s", scen[i].galaxy);
    for (int i = 0; i < NVALUES; i++) gd.value[i] = gd_values[i].init;
    gd.left = 4; gd.left_supply = 4; /* GameDataPlayerStatus::initializeData */
    gd.ok = 1;
}

void gd_attach(const Scene *s)
{
    const u8 *g = s->gdat;
    story = NULL; info = NULL; scen = NULL; nstory = ninfo = nscen = 0;
    if (!g) return;
    nstory = (int)((const u32 *)g)[0]; ninfo = (int)((const u32 *)g)[1]; nscen = (int)((const u32 *)g)[2];
    story = (const GStory *)(g + 16);
    info = (const GInfo *)(story + nstory);
    scen = (const GScen *)(info + ninfo);
    if (!gd.ok) gd_reset();
}

int gd_has_star(const char *g, int star) { int i = find_gal(g); return i >= 0 && star >= 1 && star <= 8 && (gd.gal[i].owned >> (star - 1) & 1); }
void gd_set_star(const char *g, int star) { int i = find_gal(g); if (i >= 0 && star >= 1 && star <= 8) gd.gal[i].owned |= (u8)(1 << (star - 1)); }
int gd_star_num_galaxy(const char *g)
{
    int i = find_gal(g), n = 0, k = gd_power_star_num(g);
    for (int s = 1; i >= 0 && s <= k; s++) n += gd.gal[i].owned >> (s - 1) & 1;
    return n;
}
int gd_star_num(void)
{
    int n = 0;
    for (int i = 0; i < gd.ngal; i++) n += gd_star_num_galaxy(gd.gal[i].name);
    return n;
}
int gd_visited(const char *g, int sc) { int i = find_gal(g); return i < 0 || (sc >= 1 && sc <= 8 && (gd.gal[i].visited >> (sc - 1) & 1)); }
int gd_visited_any(const char *g) { int i = find_gal(g); return i < 0 || gd.gal[i].visited != 0; }
int gd_stars_to_open(const char *g) { const GInfo *gi = find_info(g); return gi ? gi->open_stars : 0; }
void gd_set_visited(const char *g, int sc) { int i = find_gal(g); if (i >= 0 && sc >= 1 && sc <= 8) gd.gal[i].visited |= (u8)(1 << (sc - 1)); }
int gd_max_coins(const char *g, int sc) { int i = find_gal(g); return i >= 0 && sc >= 1 && sc <= 8 ? gd.gal[i].max_coin[sc - 1] : 0; }
void gd_update_max_coins(const char *g, int sc, int coins)
{
    int i = find_gal(g);
    if (i >= 0 && sc >= 1 && sc <= 8 && coins > gd.gal[i].max_coin[sc - 1]) gd.gal[i].max_coin[sc - 1] = (u16)(coins > 999 ? 999 : coins);
}

int gd_story_passed(const char *event)
{
    for (int i = 0; i < nstory; i++)
        if (!strcmp(story[i].name, event)) return story[i].progress <= gd.story;
    return 0 <= gd.story; /* not in the table: progress 0 */
}
void gd_story_follow(const char *event)
{
    for (int i = 0; i < nstory; i++)
        if (!strcmp(story[i].name, event)) { gd.story = (u8)story[i].progress; return; }
}

static int find_value(const char *name)
{
    for (int i = 0; i < NVALUES; i++)
        if (!strcmp(gd_values[i].name, name)) return i;
    return -1;
}
int gd_value(const char *name) { int i = find_value(name); return i < 0 ? 0 : gd.value[i]; }
void gd_set_value(const char *name, int v) { int i = find_value(name); if (i >= 0) gd.value[i] = (u16)v; }

/* GameEventFlagTable::getGalaxyDependedFlags: the galaxy's OpenCondition0..2 */
static int galaxy_open_by_flags(const char *g)
{
    const GInfo *gi = find_info(g);
    for (int c = 0; gi && c < 3; c++)
        if (gi->cond[c][0] && !gd_flag_is_on(gi->cond[c])) return 0;
    return 1;
}
static int open_stars(const char *g) { const GInfo *gi = find_info(g); return gi ? gi->open_stars : 0; }

int gd_flag_known(const char *name) { return find_flag(name) >= 0; }
int gd_galaxy_count(void) { return ninfo; }
void gd_galaxy_at(int i, const char **name, const char **map_pane) /* cGalaxyInfoTable's row i: name, MapPaneName */
{
    *name = i >= 0 && i < ninfo ? info[i].name : "";
    *map_pane = i >= 0 && i < ninfo ? info[i].pane : "";
}
int gd_flag_can_on(const char *name) /* GameEventFlagChecker::canOn */
{
    int i = find_flag(name);
    if (i < 0) return 0;
    const GdFlag *f = &gd_flags[i];
    switch (f->type) {
    case GT_NONE: return 1;
    case GT_STARNUM: return gd_star_num() >= f->c1;
    case GT_GALAXYOPENSTAR: return open_stars(f->s1) <= gd_star_num();
    case GT_SPECIALSTAR: return gd_has_star(f->s1, f->c1);
    case GT_EVENTFLAG: return (!f->s1 || gd_flag_is_on(f->s1)) && (!f->s2 || gd_flag_is_on(f->s2));
    case GT_STORYEVENT: return gd_story_passed(f->s1);
    case GT_GALAXY: return gd_star_num() >= open_stars(name) && galaxy_open_by_flags(name); /* isOnGalaxy */
    case GT_COMET: return gd_flag_is_on(f->s2) && gd_has_star(f->s1, f->c1);              /* isOnComet */
    case GT_STORYEVENTSYNC: return gd_flag_is_on(f->s1);
    case GT_STARPIECE: return f->c1 + 8 < 16 && gd.alms[f->c1 + 8] >= f->c2;
    case GT_EVENTVALUEISZERO: return gd_flag_is_on(f->s2) && gd_value(f->s1) == 0;
    case GT_COMPLETEMARIOANDLUIGI: return gd.complete_ml;
    }
    return 0;
}

int gd_flag_is_on(const char *name) /* GameEventFlagChecker::isOn: the computed ones (save flag 1) asked, the rest stored */
{
    int i = find_flag(name);
    if (i < 0) return 0;
    if (gd_flags[i].save & 1) return gd_flag_can_on(name);
    switch (gd_flags[i].type) {
    case GT_COMET: case GT_STARPIECE: case GT_COMPLETEMARIOANDLUIGI: case GT_STORYEVENTSYNC: return 0;
    }
    return gd.flag[i >> 3] >> (i & 7) & 1;
}

/* GameDataFunction::getSceneStartGameDataHolder: the game data as the scene started (gd_scene_start), for the
 * isOnJust / canOnJust checks of the story (a flag that went on in this scene) */
static typeof(gd) gd_start;
void gd_scene_start(void) { gd_start = gd; }
static int at_start(int (*f)(const char *), const char *name)
{
    typeof(gd) cur = gd;
    gd = gd_start;
    int r = f(name);
    gd = cur;
    return r;
}
int gd_flag_is_on_just(const char *name) { return gd_flag_is_on(name) && !at_start(gd_flag_is_on, name); }    /* isOnJustGameEventFlag */
int gd_flag_can_on_just(const char *name) { return gd_flag_can_on(name) && !at_start(gd_flag_can_on, name); } /* canOnJustGameEventFlag */
static int green_num(const char *unused) /* GameDataFunction::calcGreenStarNum: SpecialStarGreen1..3 */
{
    (void)unused;
    return gd_flag_is_on("SpecialStarGreen1") + gd_flag_is_on("SpecialStarGreen2") + gd_flag_is_on("SpecialStarGreen3");
}
int gd_green_first_just(void) { return at_start(green_num, "") == 0 && green_num("") == 1; } /* isJustGetGreenStarFirst */

/* GameEventFlagTable::getExclamationGalaxyNameFromIndex / getStarPieceNumGivingToTicoGalaxy / ...Max: the i-th
 * StarPiece flag's galaxy (a hungry Luma's), the star pieces given to it (StarPieceAlmsStorage i + 8) and its need */
int gd_tico_galaxy(int i, const char **g, int *given, int *max)
{
    for (int k = 0; k < NFLAGS; k++) {
        const GdFlag *f = &gd_flags[k];
        if (f->type != GT_STARPIECE || f->c1 != i) continue;
        *g = f->s1; *given = i + 8 < 16 ? gd.alms[i + 8] : 0; *max = f->c2;
        return 1;
    }
    return 0;
}

int gd_flag_try_on(const char *name) /* GameEventFlagChecker::tryOn */
{
    if (!gd_flag_can_on(name)) return 0;
    int i = find_flag(name);
    if (gd_flags[i].save & 1) return 0;
    switch (gd_flags[i].type) {
    case GT_SPECIALSTAR: case GT_STORYEVENT: case GT_COMET: case GT_STARPIECE: case GT_COMPLETEMARIOANDLUIGI:
    case GT_STORYEVENTSYNC: return 0;
    }
    gd.flag[i >> 3] |= (u8)(1 << (i & 7));
    return 1;
}

int gd_grand_galaxy_no(const char *g) { const GInfo *gi = find_info(g); return gi ? gi->grand : 0; }
int gd_is_grand_star(const char *g, int star) /* isPowerStarSpecial(..., "SpecialStarGrand") */
{
    for (int i = 0; i < NFLAGS; i++)
        if (gd_flags[i].type == GT_SPECIALSTAR && gd_flags[i].c1 == star && gd_flags[i].s1 && !strcmp(gd_flags[i].s1, g) &&
            strstr(gd_flags[i].name, "SpecialStarGrand"))
            return 1;
    return 0;
}
int gd_galaxy_open(const char *g) { return !gd_power_star_num(g) || gd_flag_is_on(g); }
int gd_galaxy_appear(const char *g)
{
    char n[64];
    snprintf(n, sizeof n, "Appear%s", g);
    return find_flag(n) < 0 || gd_flag_is_on(n);
}

int gd_player_left(void) { return gd.left > 99 ? 99 : gd.left; }
void gd_add_player_left(int n) { int v = gd.left + n; gd.left = (u16)(v < 0 ? 0 : v > 99 ? 99 : v); }
int gd_stocked_star_pieces(void) { return (int)gd.pieces; }
void gd_add_stocked_star_pieces(int n) { int v = (int)gd.pieces + n; gd.pieces = (u32)(v < 0 ? 0 : v > 9999 ? 9999 : v); }

/* the file: "SMGD", a version, the struct (ponytail: not GameDataHolder's BinaryDataChunkHolder chunks) */

/* GalaxyCometScheduler.cpp: each grand galaxy's timetable of comets (0 normal, 1 the 100-coin one); a table's comet
 * lands on its galaxy until a stage clear moves it on (forceToNextState: land -> hide -> the next one that can
 * appear lands). Its state in the game data (updateGalaxyCometStatus) */
typedef struct { const char *g; u8 cat; } CometPaper;
#define N 0
#define P 1
static const CometPaper ct1[] = {{"EggStarGalaxy", N}, {"HoneyBeeKingdomGalaxy", N}, {"HoneyBeeKingdomGalaxy", P}, {"EggStarGalaxy", P},
    {"EggStarGalaxy", N}, {"HoneyBeeKingdomGalaxy", N}, {"EggStarGalaxy", P}, {"HoneyBeeKingdomGalaxy", P}, {"EggStarGalaxy", N},
    {"HoneyBeeKingdomGalaxy", P}, {"EggStarGalaxy", P}, {"HoneyBeeKingdomGalaxy", N}, {"HoneyBeeKingdomGalaxy", N}, {"EggStarGalaxy", N},
    {"HoneyBeeKingdomGalaxy", P}, {"EggStarGalaxy", P}, {NULL, 0}};
static const CometPaper ct2[] = {{"StarDustGalaxy", N}, {"StarDustGalaxy", P}, {"BattleShipGalaxy", N}, {"BattleShipGalaxy", P},
    {"StarDustGalaxy", P}, {"BattleShipGalaxy", N}, {"StarDustGalaxy", N}, {"BattleShipGalaxy", P}, {"StarDustGalaxy", N},
    {"StarDustGalaxy", P}, {"BattleShipGalaxy", N}, {"BattleShipGalaxy", P}, {"StarDustGalaxy", N}, {"BattleShipGalaxy", N},
    {"StarDustGalaxy", P}, {"BattleShipGalaxy", P}, {NULL, 0}};
static const CometPaper ct3[] = {{"PhantomGalaxy", N}, {"HeavenlyBeachGalaxy", N}, {"PhantomGalaxy", P}, {"HeavenlyBeachGalaxy", P},
    {"HeavenlyBeachGalaxy", N}, {"PhantomGalaxy", P}, {"HeavenlyBeachGalaxy", P}, {"PhantomGalaxy", N}, {"HeavenlyBeachGalaxy", P},
    {"PhantomGalaxy", N}, {"HeavenlyBeachGalaxy", N}, {"PhantomGalaxy", P}, {"PhantomGalaxy", N}, {"HeavenlyBeachGalaxy", P},
    {"PhantomGalaxy", P}, {"HeavenlyBeachGalaxy", N}, {NULL, 0}};
static const CometPaper ct4[] = {{"SandClockGalaxy", N}, {"CosmosGardenGalaxy", N}, {"IceVolcanoGalaxy", N}, {"CosmosGardenGalaxy", P},
    {"SandClockGalaxy", N}, {"CosmosGardenGalaxy", N}, {"SandClockGalaxy", P}, {"CosmosGardenGalaxy", P}, {"SandClockGalaxy", N},
    {"IceVolcanoGalaxy", P}, {"SandClockGalaxy", P}, {"CosmosGardenGalaxy", N}, {"IceVolcanoGalaxy", N}, {"CosmosGardenGalaxy", P},
    {"IceVolcanoGalaxy", P}, {"SandClockGalaxy", N}, {"IceVolcanoGalaxy", N}, {"CosmosGardenGalaxy", P}, {"SandClockGalaxy", P},
    {"IceVolcanoGalaxy", P}, {"CosmosGardenGalaxy", N}, {"IceVolcanoGalaxy", N}, {"SandClockGalaxy", P}, {"IceVolcanoGalaxy", P}, {NULL, 0}};
static const CometPaper ct5[] = {{"OceanRingGalaxy", P}, {"ReverseKingdomGalaxy", N}, {"OceanRingGalaxy", N}, {"ReverseKingdomGalaxy", P},
    {"FactoryGalaxy", N}, {"OceanRingGalaxy", P}, {"FactoryGalaxy", N}, {"ReverseKingdomGalaxy", N}, {"FactoryGalaxy", P},
    {"OceanRingGalaxy", N}, {"ReverseKingdomGalaxy", N}, {"FactoryGalaxy", P}, {"OceanRingGalaxy", N}, {"ReverseKingdomGalaxy", P},
    {"FactoryGalaxy", N}, {"ReverseKingdomGalaxy", N}, {"FactoryGalaxy", P}, {"OceanRingGalaxy", P}, {"FactoryGalaxy", N},
    {"ReverseKingdomGalaxy", P}, {"OceanRingGalaxy", N}, {"FactoryGalaxy", P}, {"OceanRingGalaxy", P}, {"ReverseKingdomGalaxy", P}, {NULL, 0}};
static const CometPaper ct6[] = {{"CannonFleetGalaxy", P}, {"OceanPhantomCaveGalaxy", P}, {"CannonFleetGalaxy", N}, {"HellProminenceGalaxy", N},
    {"CannonFleetGalaxy", P}, {"HellProminenceGalaxy", P}, {"OceanPhantomCaveGalaxy", N}, {"CannonFleetGalaxy", P},
    {"OceanPhantomCaveGalaxy", P}, {"CannonFleetGalaxy", P}, {"HellProminenceGalaxy", N}, {"OceanPhantomCaveGalaxy", P},
    {"CannonFleetGalaxy", N}, {"HellProminenceGalaxy", P}, {"OceanPhantomCaveGalaxy", N}, {"HellProminenceGalaxy", N},
    {"CannonFleetGalaxy", N}, {"OceanPhantomCaveGalaxy", N}, {"HellProminenceGalaxy", P}, {"CannonFleetGalaxy", N},
    {"HellProminenceGalaxy", N}, {"OceanPhantomCaveGalaxy", P}, {"HellProminenceGalaxy", P}, {"OceanPhantomCaveGalaxy", N}, {NULL, 0}};
#undef N
#undef P
static const CometPaper *const comet_tables[6] = {ct1, ct2, ct3, ct4, ct5, ct6};

static int comet_can(const CometPaper *p) /* canAppearNormalComet / canAppearCoin100Comet */
{
    char f[64];
    snprintf(f, sizeof f, "%s%s", p->cat ? "Coin100Comet" : "NormalComet", p->g);
    return gd_flag_is_on(f) && !gd_has_star(p->g, p->cat ? 5 : 4);
}
static int comet_find(int t, int start, int *pos) /* findSuitableTimePaperPos */
{
    const CometPaper *tp = comet_tables[t];
    int len = 0, p = start;
    while (tp[len].g) len++;
    for (int i = 0; i < len; i++) {
        if (!tp[++p].g) p = 0;
        if (comet_can(&tp[p])) { *pos = p; return 1; }
    }
    *pos = p;
    return 0;
}
void gd_comet_sync(void) /* syncWithFlags: checkIsReady; "EventCometStarter" just on: the first contact */
{
    for (int t = 0; t < 6; t++) {
        int pos, ready = comet_find(t, -1, &pos);
        if (ready != gd.comet_ready[t]) { gd.comet_ready[t] = (u8)ready; if (ready) gd.comet_pos[t] = (u8)pos; }
    }
    if (gd_flag_is_on("EventCometStarter") && !gd.comet_started) { /* setScheduleFirstContact */
        gd.comet_started = 1;
        for (int t = 0; t < 6; t++) { gd.comet_pos[t] = 0; gd.comet_land[t] = t == 0; }
    }
}
void gd_comet_next(void) /* forceToNextState: each table land <-> hide; hidden: the next comet that can appear */
{
    for (int t = 0; t < 6; t++) {
        if (!gd.comet_ready[t]) continue;
        gd.comet_land[t] = !gd.comet_land[t];
        if (!gd.comet_land[t]) { int pos; gd.comet_ready[t] = (u8)comet_find(t, gd.comet_pos[t], &pos); gd.comet_pos[t] = (u8)pos; }
    }
}
int gd_comet_star(const char *g) /* isCometLand: its comet's star (getEncounterCometPowerStarId: 4 normal, 5 coins); 0 none */
{
    for (int t = 0; t < 6; t++) {
        const CometPaper *p = &comet_tables[t][gd.comet_pos[t]];
        if (gd.comet_ready[t] && gd.comet_land[t] && !strcmp(p->g, g)) return p->cat ? 5 : 4;
    }
    return 0;
}

/* SaveDataHandleSequence's image of the save (config%d + mario%d per UserFile 1..6): each file a serialized gd, created
 * or not, its icon (FileSelectIconID: 1 Mario .. 5 Peach) and a modification count for its date. ponytail: no Luigi
 * data (luigi%d), no sysconf; the memory card writes this image (gd_image) */
#define GD_FILES 6
static struct { u8 created, icon, pad[2]; u32 modified; u8 data[8 + sizeof gd]; } files[GD_FILES];
static int cur_file = -1;
static u32 modify_count;

int gd_file_created(int i) { return i >= 0 && i < GD_FILES && files[i].created; }
int gd_file_icon(int i) { return gd_file_created(i) ? files[i].icon : 0; }
u32 gd_file_modified(int i) { return gd_file_created(i) ? files[i].modified : 0; }
static void file_store(int i) { gd_serialize(files[i].data, sizeof files[i].data); files[i].modified = ++modify_count; }
void gd_file_create(int i, int icon) /* SaveDataHandler: resetAllData, setCreated, updateLastModified */
{
    if (i < 0 || i >= GD_FILES) return;
    char keep[sizeof gd];
    memcpy(keep, &gd, sizeof gd);
    gd_reset();
    files[i].created = 1; files[i].icon = (u8)icon;
    file_store(i);
    memcpy(&gd, keep, sizeof gd);
}
void gd_file_copy(int dst, int src) /* copyUserFileMemory: every entry of src into dst */
{
    if (dst < 0 || dst >= GD_FILES || !gd_file_created(src)) return;
    memcpy(&files[dst], &files[src], sizeof files[dst]);
    files[dst].modified = ++modify_count;
}
void gd_file_delete(int i) { if (i >= 0 && i < GD_FILES) memset(&files[i], 0, sizeof files[i]); }
void gd_file_set_icon(int i, int icon) { if (gd_file_created(i)) { files[i].icon = (u8)icon; files[i].modified = ++modify_count; } }
int gd_file_load(int i) /* restoreUserFile into the current data (startGameDataLoadSequence) */
{
    if (!gd_file_created(i) || !gd_deserialize(files[i].data, sizeof files[i].data)) return 0;
    cur_file = i;
    return 1;
}
void gd_file_save(void) { if (cur_file >= 0) file_store(cur_file); } /* the current game into its file */
int gd_file_current(void) { return cur_file; }
void gd_file_info(int i, int *stars, int *pieces, int *normal_end, int *complete_end) /* FileSelector::setFileInfo */
{
    static char keep[sizeof gd];
    memcpy(keep, &gd, sizeof gd);
    *stars = *pieces = *normal_end = *complete_end = 0;
    if (gd_file_created(i) && gd_deserialize(files[i].data, sizeof files[i].data)) {
        *stars = gd_star_num(); *pieces = (int)gd.pieces;
        *normal_end = gd_flag_is_on("ViewNormalEnding"); *complete_end = gd_flag_is_on("ViewCompleteEnding");
    }
    memcpy(&gd, keep, sizeof gd);
}
u8 *gd_image(int *size) { *size = (int)sizeof files; return (u8 *)files; } /* the whole save, for the memory card */
void gd_image_loaded(void) { for (int i = 0; i < GD_FILES; i++) if (files[i].modified > modify_count) modify_count = files[i].modified; }

int gd_serialize(u8 *buf, int size)
{
    int n = 8 + (int)sizeof gd;
    if (size < n) return 0;
    memcpy(buf, "SMGD", 4);
    buf[4] = 2; buf[5] = buf[6] = buf[7] = 0; /* 2: the comets kept */
    memcpy(buf + 8, &gd, sizeof gd);
    return n;
}
int gd_deserialize(const u8 *buf, int size)
{
    if (size < 8 + (int)sizeof gd || memcmp(buf, "SMGD", 4) || buf[4] != 2) return 0;
    memcpy(&gd, buf + 8, sizeof gd);
    gd.left = 4; /* GameDataPlayerStatus::deserialize: lives back to 4 */
    return 1;
}
