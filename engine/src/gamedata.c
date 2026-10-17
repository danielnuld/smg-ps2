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
int gd_serialize(u8 *buf, int size)
{
    int n = 8 + (int)sizeof gd;
    if (size < n) return 0;
    memcpy(buf, "SMGD", 4);
    buf[4] = 1; buf[5] = buf[6] = buf[7] = 0;
    memcpy(buf + 8, &gd, sizeof gd);
    return n;
}
int gd_deserialize(const u8 *buf, int size)
{
    if (size < 8 + (int)sizeof gd || memcmp(buf, "SMGD", 4) || buf[4] != 1) return 0;
    memcpy(&gd, buf + 8, sizeof gd);
    gd.left = 4; /* GameDataPlayerStatus::deserialize: lives back to 4 */
    return 1;
}
