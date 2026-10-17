#include "menu.h"

#include "anim.h"
#include "game.h"
#include "gamedata.h"
#include "sequence.h"
#include "lyt.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "se.h"
#include "se_ids.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void fade_update(void);
static void fade_draw(void);
static void ss_init(const Scene *s);
static void ss_update(void);
static void wipe_update(void);
static void wipe_draw_quad(void);

/* the screen wipe of the select (closeSystemWipeWhiteFade / closeSystemWipeFadeWithCaptureScreen and the opens) */
static struct { int t, frames, close; u32 rgb; } wipe;
static void wipe_start(int frames, int close, u32 rgb) { wipe.t = 0; wipe.frames = frames; wipe.close = close; wipe.rgb = rgb; }
static int wipe_active(void) { return wipe.frames && wipe.t < wipe.frames; }

/* --- messages (tools/lyt.py MSGS) ------------------------------------------------------------------------------ */
static const u8 *msgs;

const u16 *msg_get(const char *label)
{
    if (!msgs || !label) return NULL;
    int lo = 0, hi = (int)*(const u32 *)msgs - 1;
    const u32 *e = (const u32 *)(msgs + 16);
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = strcmp((const char *)(msgs + e[2 * m]), label);
        if (!c) return (const u16 *)(msgs + e[2 * m + 1]);
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* --- nerves (Spine: setNerve takes effect at the end of the update, its step 0 seen from outside until the next) --- */
typedef struct { int cur, next, step; } Nrv;
static void nrv_set(Nrv *n, int v) { n->next = v; n->step = -1; }
static void nrv_change(Nrv *n) { if (n->next >= 0) { n->cur = n->next; n->next = -1; n->step = 0; } }
static int nrv_begin(Nrv *n) { nrv_change(n); return n->cur; } /* Spine::update: change, execute, */
static void nrv_end(Nrv *n) { n->step++; nrv_change(n); }      /* step on, change */
static int nrv_is(const Nrv *n, int v) { return (n->next >= 0 ? n->next : n->cur) == v; } /* getCurrentNerve */
#define FIRST(n) ((n)->step == 0)

static int pointing_on(LytInst *l, const char *pane) /* MR::isStarPointerPointingPane */
{
    float x, y;
    game_cursor(&x, &y);
    return lyt_pane_hit(l, pane, x, y);
}

/* --- ButtonPaneController -------------------------------------------------------------------------------------- */
enum { BP_HIDDEN, BP_APPEAR, BP_WAIT, BP_POINTING, BP_NOTPOINTING, BP_DECIDED, BP_DECIDEDWAIT, BP_TODISAPPEAR, BP_DISAPPEAR };
typedef struct {
    LytInst *host;
    const char *pane, *bound;
    int ch;
    u8 to_disappear, end_disappear, pointing_kept, active, selected, pointing; /* _18, _22, _23, _24 */
    float pstart;
    Nrv n;
} Btn;

static void btn_init(Btn *b, LytInst *host, const char *pane, const char *bound, int ch, int to_disappear)
{
    memset(b, 0, sizeof *b);
    b->host = host; b->pane = pane; b->bound = bound; b->ch = ch; b->to_disappear = (u8)to_disappear; b->end_disappear = 1;
    b->n.next = -1;
    lyt_show_pane(host, pane, 0);
    nrv_set(&b->n, BP_HIDDEN);
}
static int btn_is(const Btn *b, int nrv) { return nrv_is(&b->n, nrv); }
static void btn_appear(Btn *b) { b->selected = 0; b->pstart = 0; b->pointing = 0; b->active = 1; nrv_set(&b->n, BP_APPEAR); }
static void btn_disappear(Btn *b) { b->active = 1; nrv_set(&b->n, BP_DISAPPEAR); }
static void btn_force_wait(Btn *b) { b->selected = 0; b->pstart = 0; b->pointing = 0; nrv_set(&b->n, BP_WAIT); }
static int btn_hidden(const Btn *b) { return btn_is(b, BP_HIDDEN); }
static int btn_appearing(const Btn *b) { return btn_is(b, BP_APPEAR); }
static int btn_decided_wait(const Btn *b) { return btn_is(b, BP_DECIDEDWAIT); }
static int btn_pointing_trigger(const Btn *b) { return btn_is(b, BP_POINTING) && FIRST(&b->n); }
static int btn_selected_se(const Btn *b) { return btn_is(b, BP_DECIDED) && b->n.step == 1; }
static int btn_pointing_pane(Btn *b) { return b->pointing && pointing_on(b->host, b->bound); }

static int btn_try_select(Btn *b) /* trySelect */
{
    if (!b->active) return 0;
    b->pointing = 1;
    if (btn_is(b, BP_POINTING) && pad_pressed(PAD_CROSS)) { /* testDPDMenuPadDecideTrigger: A */
        b->selected = 1;
        nrv_set(&b->n, BP_DECIDED);
        return 1;
    }
    return 0;
}

static float btn_pointing_start(const Btn *b) /* calcPointingAnimStartFrame */
{
    if (lyt_pane_stopped(b->host, b->pane, b->ch)) return 0;
    float mx = lyt_anim_frames(b->host, "ButtonSelectIn"), f = mx - lyt_pane_frame(b->host, b->pane, b->ch);
    return f < 0 ? 0 : f > mx ? mx : f;
}
static int btn_start(Btn *b, const char *anim)
{
    if (!FIRST(&b->n)) return 0;
    lyt_start_pane(b->host, b->pane, anim, b->ch);
    return 1;
}
static void btn_at_stop(Btn *b, int nrv) { if (lyt_pane_stopped(b->host, b->pane, b->ch)) nrv_set(&b->n, nrv); }
static int btn_try_pointing(Btn *b, int on)
{
    if (!on || !(btn_is(b, BP_WAIT) || btn_is(b, BP_NOTPOINTING))) return 0;
    b->pstart = btn_is(b, BP_NOTPOINTING) ? btn_pointing_start(b) : 0;
    nrv_set(&b->n, BP_POINTING);
    return 1;
}
static int btn_try_not_pointing(Btn *b, int on)
{
    if (on || !btn_is(b, BP_POINTING)) return 0;
    b->pstart = btn_pointing_start(b);
    nrv_set(&b->n, BP_NOTPOINTING);
    return 1;
}

static void btn_update(Btn *b)
{
    if (b->active) {
        switch (nrv_begin(&b->n)) {
        case BP_HIDDEN: if (FIRST(&b->n)) lyt_show_pane(b->host, b->pane, 0); break;
        case BP_APPEAR:
            if (btn_start(b, "ButtonAppear")) lyt_show_pane(b->host, b->pane, 1);
            btn_at_stop(b, BP_WAIT);
            break;
        case BP_WAIT:
            btn_start(b, "ButtonWait");
            if (!b->pointing_kept) btn_try_pointing(b, btn_pointing_pane(b));
            break;
        case BP_POINTING:
            if (btn_start(b, "ButtonSelectIn")) lyt_set_pane_frame(b->host, b->pane, b->ch, b->pstart);
            if (!b->pointing_kept) btn_try_not_pointing(b, btn_pointing_pane(b));
            else btn_try_not_pointing(b, b->pointing);
            break;
        case BP_NOTPOINTING:
            if (btn_start(b, "ButtonSelectOut")) lyt_set_pane_frame(b->host, b->pane, b->ch, b->pstart);
            if (!b->pointing_kept && btn_try_pointing(b, btn_pointing_pane(b))) break;
            btn_at_stop(b, BP_WAIT);
            break;
        case BP_DECIDED:
            btn_start(b, "ButtonDecide");
            btn_at_stop(b, b->to_disappear ? BP_TODISAPPEAR : b->end_disappear ? BP_DISAPPEAR : BP_DECIDEDWAIT);
            break;
        case BP_TODISAPPEAR:
            if (FIRST(&b->n)) { /* cDecidedToDisappearAnimRate */
                lyt_start_pane(b->host, b->pane, "ButtonSelectOut", b->ch);
                lyt_set_pane_rate(b->host, b->pane, b->ch, 1.5f);
            }
            btn_at_stop(b, b->end_disappear ? BP_DISAPPEAR : BP_DECIDEDWAIT);
            break;
        case BP_DISAPPEAR:
            btn_start(b, "ButtonEnd");
            btn_at_stop(b, BP_HIDDEN);
            break;
        }
        nrv_end(&b->n);
    }
    b->pointing = 0;
}

/* --- YesNoController ------------------------------------------------------------------------------------------- */
enum { YN_SELECTING, YN_DECIDED, YN_DISAPPEAR, YN_SELECTED, YN_NOTSELECTED };
typedef struct { LytInst *host; Btn yes, no; u8 on; Nrv n; } YesNo;

static void yn_init(YesNo *y, LytInst *host)
{
    memset(y, 0, sizeof *y);
    y->host = host;
    btn_init(&y->yes, host, "Right", "BoxRight", 0, 1); y->yes.end_disappear = 0;
    btn_init(&y->no, host, "Left", "BoxLeft", 0, 1); y->no.end_disappear = 0;
    y->n.next = YN_SELECTING;
}
static void yn_appear(YesNo *y) { y->on = 1; nrv_set(&y->n, YN_SELECTING); }
static int yn_yes(const YesNo *y) { return y->yes.selected; }
static int yn_disappear_start(const YesNo *y) { return nrv_is(&y->n, YN_DISAPPEAR) && FIRST(&y->n); }

static void yn_update(YesNo *y)
{
    if (!y->on) return;
    switch (nrv_begin(&y->n)) {
    case YN_SELECTING:
        if (FIRST(&y->n)) { btn_appear(&y->yes); btn_appear(&y->no); }
        if (btn_pointing_trigger(&y->yes) || btn_pointing_trigger(&y->no)) se_start(SE_SY_TALK_FOCUS_ITEM, NULL);
        if (btn_try_select(&y->yes) || btn_try_select(&y->no)) nrv_set(&y->n, YN_DECIDED);
        break;
    case YN_DECIDED:
        if (FIRST(&y->n)) se_start(y->yes.selected ? SE_SY_TALK_SELECT_YES : SE_SY_TALK_SELECT_NO, NULL);
        if ((y->yes.selected && btn_decided_wait(&y->yes)) || (!y->yes.selected && btn_decided_wait(&y->no)))
            nrv_set(&y->n, YN_DISAPPEAR);
        break;
    case YN_DISAPPEAR:
        if (FIRST(&y->n)) { btn_disappear(&y->yes); btn_disappear(&y->no); }
        if (btn_hidden(&y->yes) && btn_hidden(&y->no))
            nrv_set(&y->n, y->yes.selected || y->no.selected ? YN_SELECTED : YN_NOTSELECTED);
        break;
    case YN_SELECTED: case YN_NOTSELECTED: y->on = 0; break;
    }
    nrv_end(&y->n);
    btn_update(&y->yes);
    btn_update(&y->no);
}

/* --- SysInfoWindow (WindowType_Normal) ------------------------------------------------------------------------ */
enum { SI_APPEAR, SI_WAIT, SI_DISAPPEAR };
enum { SI_KEY, SI_BLOCKING, SI_YESNO };
typedef struct { LytInst *l; YesNo yn; int type; u8 alive; Nrv n; } SysInfo;

static void si_init(SysInfo *w)
{
    memset(w, 0, sizeof *w);
    w->l = lyt_new("SysInfoWindow");
    w->n.next = SI_APPEAR;
    if (!w->l) return;
    yn_init(&w->yn, w->l);
    lyt_set_text_rec(w->l, "Left", msg_get("System_Save00_No"));
    lyt_set_text_rec(w->l, "Right", msg_get("System_Save00_Yes"));
}

/* appear(message, type, TextPos_Center, MessageType_System / Game): the message and its _Yes / _No */
static void si_appear(SysInfo *w, const char *label, int type)
{
    if (!w->l) return;
    w->type = type;
    lyt_show_rec(w->l, "Left", type == SI_YESNO);
    lyt_show_rec(w->l, "Right", type == SI_YESNO);
    lyt_set_vpos_rec(w->l, "InfoWindow", 1);
    lyt_set_text_rec(w->l, "InfoWindow", msg_get(label));
    if (type == SI_YESNO) {
        static char yes[48], no[48];
        snprintf(yes, sizeof yes, "%s_Yes", label);
        snprintf(no, sizeof no, "%s_No", label);
        lyt_set_text_rec(w->l, "Right", msg_get(yes));
        lyt_set_text_rec(w->l, "Left", msg_get(no));
    }
    lyt_start_pane(w->l, "InfoWindow", "ButtonAppear", 0);
    lyt_start_pane(w->l, "WinInfoWindow", "ButtonAppear", 0);
    nrv_set(&w->n, SI_APPEAR);
    w->alive = 1;
    if (type == SI_YESNO) yn_appear(&w->yn);
}

static void si_update(SysInfo *w)
{
    if (!w->alive) return;
    switch (nrv_begin(&w->n)) { /* LayoutActor::movement: the nerve, control (the yes / no buttons), the frames */
    case SI_APPEAR:
        if (lyt_pane_stopped(w->l, "InfoWindow", 0)) nrv_set(&w->n, SI_WAIT);
        break;
    case SI_WAIT:
        if (FIRST(&w->n)) {
            lyt_start_pane(w->l, "InfoWindow", "ButtonWait", 0);
            lyt_start_pane(w->l, "WinInfoWindow", "ButtonWait", 0);
        }
        if (w->type == SI_KEY) {
            if (pad_pressed(PAD_CROSS)) { se_start(SE_SY_TALK_OK, NULL); nrv_set(&w->n, SI_DISAPPEAR); }
        } else if (yn_disappear_start(&w->yn)) nrv_set(&w->n, SI_DISAPPEAR);
        break;
    case SI_DISAPPEAR:
        if (FIRST(&w->n)) {
            lyt_start_pane(w->l, "InfoWindow", "ButtonEnd", 0);
            lyt_start_pane(w->l, "WinInfoWindow", "ButtonEnd", 0);
        }
        if (lyt_pane_stopped(w->l, "InfoWindow", 0) && (w->type != SI_YESNO || !w->yn.on)) w->alive = 0;
        break;
    }
    nrv_end(&w->n);
    yn_update(&w->yn);
    lyt_update(w->l);
}

/* --- PauseMenu ------------------------------------------------------------------------------------------------- */
enum { PM_SELECTING, PM_DECIDED, PM_DISAPPEAR, PM_CONFIRM };
static struct {
    LytInst *l;
    Btn up, down;
    SysInfo win;
    u8 alive, back_astro;
    int fade; /* closeSystemWipeFadeWithCaptureScreen: frames into the fade before the scene changes */
    Nrv n;
    u16 piece_msg[3];
} pm;

static int stage_astro(void) /* MR::isStageAstroLocation */
{
    return !strncmp(game_stage_name, "AstroGalaxy", 11) || !strncmp(game_stage_name, "AstroDome", 9) ||
           !strncmp(game_stage_name, "LibraryRoom", 11);
}
/* isInvalidBackAstroDome: !isOnGameEventFlagUseAstroDome (the story past バトラー情報Ａ) or in the Observatory (but
 * the Gateway, HeavensDoorGalaxy) */
static int pm_invalid_back(void)
{
    return !gd_story_passed("バトラー情報Ａ") || (stage_astro() && strncmp(game_stage_name, "HeavensDoorGalaxy", 17));
}
static int pm_hide_scenario(void) { return stage_astro() || !strncmp(game_stage_name, "FileSelect", 10); } /* isStageHideScenarioTitle */

static void pm_init(void)
{
    memset(&pm, 0, sizeof pm);
    pm.n.next = PM_SELECTING;
    pm.l = lyt_new("PauseMenu");
    if (!pm.l) return;
    btn_init(&pm.up, pm.l, "NBackUp", "BoxButton1", 0, 1); pm.up.end_disappear = 0;
    btn_init(&pm.down, pm.l, "NBackDown", "BoxButton2", 0, 1); pm.down.end_disappear = 0;
    lyt_set_text_rec(pm.l, "NBackDown", msg_get(pm_invalid_back() ? "PauseMenu_ButtonEndGame" : "PauseMenu_ButtonBackAstroDome"));
    char gal[48], lbl[80];
    const char *slash = strchr(game_stage_name, '/');
    int gl = slash ? (int)(slash - game_stage_name) : (int)strlen(game_stage_name);
    snprintf(gal, sizeof gal, "%.*s", gl, game_stage_name);
    snprintf(lbl, sizeof lbl, "GalaxyName_%s", gal);
    lyt_set_text_rec(pm.l, "ShaStageTitle", msg_get(lbl));
    if (pm_hide_scenario()) lyt_show_rec(pm.l, "ScenarioTitle", 0);
    else {
        snprintf(lbl, sizeof lbl, "ScenarioName_%s%s", gal, slash ? slash + 1 : "1");
        lyt_set_text_rec(pm.l, "ScenarioTitle", msg_get(lbl));
    }
    /* updateStarPane: the galaxy's stars, the ones got lit; the extra ones (comets, hidden) after the normal ones
     * when got; only the first while the dome cannot be used */
    static const char *sha[7] = {"ShaStarA", "ShaStarB", "ShaStarC", "ShaStarD", "ShaStarE", "ShaStarF", "ShaStarG"};
    static const char *pic[7] = {"PicStarA", "PicStarB", "PicStarC", "PicStarD", "PicStarE", "PicStarF", "PicStarG"};
    for (int i = 0; i < 7; i++) lyt_show_rec(pm.l, sha[i], 0);
    if (!pm_hide_scenario()) {
        int extra = 0, invalid_dome = !gd_story_passed("バトラー情報Ａ"), normal = 0;
        int nsc = gd_scenario_num(gal), nstar = gd_power_star_num(gal);
        for (int i = 1; i <= nsc; i++) normal += !gd_scenario_comet(gal, i)[0]; /* getNormalScenarioNum */
        for (int i = 0; i < 7; i++) {
            if (i < nstar) {
                if (gd_has_star(gal, i + 1)) {
                    if (i < normal) lyt_show_rec(pm.l, sha[i], 1);
                    else lyt_show_rec(pm.l, sha[normal + extra++], 1);
                } else if (i < normal) {
                    lyt_show_rec(pm.l, sha[i], 1);
                    lyt_show_rec(pm.l, pic[i], 0);
                }
            }
            if (invalid_dome) break;
        }
        lyt_start_pane(pm.l, "Stars", "Star", 1);
        lyt_set_pane_frame_stop(pm.l, "Stars", 1, invalid_dome ? 0.0f : (float)(normal + extra - 1));
    }
    lyt_show_rec(pm.l, "LetterButton", 0); /* no Luigi letter (MR::getLuigiLetterGalaxyName) */
    si_init(&pm.win);
}

static void pm_anim_without_button(const char *anim) /* startPaneAnimWithoutButton */
{
    static const char *p[5] = {"BG", "PicStagebase", "PicStagebase1", "Stars", "Counter"};
    for (int i = 0; i < 5; i++) lyt_start_pane(pm.l, p[i], anim, 0);
}
static int pm_stopped_without_button(void)
{
    static const char *p[5] = {"BG", "PicStagebase", "PicStagebase1", "Stars", "Counter"};
    for (int i = 0; i < 5; i++) if (!lyt_pane_stopped(pm.l, p[i], 0)) return 0;
    return 1;
}

static void pm_appear(void)
{
    lyt_set_number_rec(pm.l, "ShaStarNum", gd_star_num());
    lyt_set_number_rec(pm.l, "ShaMarioNum", game_lives());
    lyt_set_number_rec(pm.l, "ShaCoinNum", game_coins());
    lyt_set_number_rec(pm.l, "ShaPieceNum", game_starpieces());
    lyt_set_text_rec(pm.l, "ShaMario", msg_get("PlayerLeft"));
    pm.piece_msg[0] = pm_hide_scenario() ? 0x57 : 0x3B; pm.piece_msg[1] = 0x40; pm.piece_msg[2] = 0;
    lyt_set_text_rec(pm.l, "ShaPiece", pm.piece_msg);
    pm_anim_without_button("Appear");
    btn_appear(&pm.up);
    btn_appear(&pm.down);
    nrv_set(&pm.n, PM_SELECTING);
    pm.alive = 1;
    se_start(SE_SY_PAUSE_ON, NULL);
}

static void pm_update(void)
{
    switch (nrv_begin(&pm.n)) {
    case PM_SELECTING:
        if (btn_pointing_trigger(&pm.up) || btn_pointing_trigger(&pm.down)) se_start(SE_SY_SELECT_PAUSE_ITEM, NULL);
        if (btn_try_select(&pm.up) || btn_try_select(&pm.down)) { nrv_set(&pm.n, PM_DECIDED); break; }
        if (pm_stopped_without_button() && !btn_appearing(&pm.up) && !btn_appearing(&pm.down) && pad_pressed(PAD_START)) {
            se_start(SE_SY_PAUSE_OFF, NULL); /* Plus / Minus: back to the game */
            nrv_set(&pm.n, PM_DISAPPEAR);
        }
        break;
    case PM_DECIDED:
        if (pm.up.selected) {
            if (btn_selected_se(&pm.up)) se_start(SE_SY_PAUSE_OFF, NULL);
            if (btn_decided_wait(&pm.up)) nrv_set(&pm.n, PM_DISAPPEAR);
        } else if (pm.down.selected && btn_decided_wait(&pm.down)) nrv_set(&pm.n, PM_CONFIRM); /* ponytail: no save first */
        break;
    case PM_DISAPPEAR:
        if (FIRST(&pm.n)) { pm_anim_without_button("End"); btn_disappear(&pm.up); btn_disappear(&pm.down); }
        if (pm_stopped_without_button() && btn_hidden(&pm.up) && btn_hidden(&pm.down)) pm.alive = 0;
        break;
    case PM_CONFIRM:
        if (FIRST(&pm.n)) si_appear(&pm.win, pm_invalid_back() ? "PauseMenu_ConfirmEndGame" : "PauseMenu_ConfirmBackAstroDome", SI_YESNO);
        if (!pm.win.alive && !pm.fade) {
            if (yn_yes(&pm.win.yn)) pm.fade = 1; /* closeSystemWipeFadeWithCaptureScreen(90), then the scene */
            else { btn_force_wait(&pm.up); btn_force_wait(&pm.down); nrv_set(&pm.n, PM_SELECTING); }
        }
        break;
    }
    nrv_end(&pm.n);
    btn_update(&pm.up);
    btn_update(&pm.down);
    lyt_update(pm.l);
    si_update(&pm.win);
    if (pm.fade == 1) { /* closeSystemWipeFadeWithCaptureScreen(90), then requestChangeSceneTitle / ...GoBackAstroDome */
        pm.fade = 0;
        menu_fade_move(90, pm_invalid_back() ? SEQ_TITLE : SEQ_GALAXY_OUT, NULL, 1, -1, 0);
    }
}

/* --- GamePauseSequence ----------------------------------------------------------------------------------------- */
void menu_init(const Scene *s)
{
    msgs = s->msgs;
    pm_init();
    ss_init(s);
    u32 rgb;
    int open = seq_take_open_wipe(&rgb); /* the wipe the last scene closed opens on this one */
    if (open) wipe_start(open, 0, rgb);
}

int menu_paused(void) { return pm.alive; }

int menu_test_point(const char *pane)
{
    float x, y;
    LytInst *l = pm.win.alive && lyt_pane(pm.win.l, pane) >= 0 ? pm.win.l : pm.l;
    if (!lyt_pane_center(l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}

int menu_update(void)
{
    fade_update();
    wipe_update();
    if (menu_scenario_selecting()) { game_cursor_menu(); ss_update(); return 1; }
    if (!pm.alive) { /* GameScenePauseControl: Plus (START) where GameScene::isPermitToPauseMenu */
        if (!pm.l || !pad_pressed(PAD_START) || !game_pause_permitted()) return 0;
        pad_eat(PAD_START);
        pm_appear();
    }
    game_cursor_menu();
    pm_update();
    return 1;
}

/* the system wipe: black over `frames`, then the sequence's move */
static struct { int t, frames, type, scenario, id, zone; char stage[32]; } fade;

void menu_fade_move(int frames, int type, const char *stage, int scenario, int start_id, int start_zone)
{
    if (fade.frames) return;
    fade.t = 0; fade.frames = frames < 1 ? 1 : frames; fade.type = type; fade.scenario = scenario;
    fade.id = start_id; fade.zone = start_zone;
    snprintf(fade.stage, sizeof fade.stage, "%s", stage ? stage : "");
}
int menu_fading(void) { return fade.frames != 0; }

static void fade_update(void)
{
    if (!fade.frames) return;
    if (++fade.t == fade.frames + 2) seq_galaxy_move(fade.type, fade.stage[0] ? fade.stage : NULL, fade.scenario, fade.id, fade.zone);
}

static void fade_draw(void)
{
    if (!fade.frames) return;
    int a = fade.t * 128 / fade.frames;
    Quad q = {0, 0, 640, 448, 0, 0, 0, 0, (u32)(a > 128 ? 128 : a) << 24, NO_TEX, 0, 0, 0}; /* after the glyphs */
    render_quads(&q, 1);
}

/* --- ScenarioSelectLayout / ScenarioSelectStar / BackButton --------------------------------------------------------
 * ScenarioSelectScene over the stage left (its world frozen and not drawn): the sky (ScenarioSelectSky, BTK) and the
 * stars (SelectPowerStar) under the camera at (0, 0, 1000) looking at the origin, fovy 45; the stars at their panes
 * (calcWorldPositionFromScreenPos, depth 1000). ponytail: the scenario never decided (no comet warning: the
 * dome's comet galaxies not chosen yet), no ScenarioSelectEffect / star effects, no BGM, the star's colour (BTP: all
 * yellow); no race best times (BestTime stays out); isPlacedCoin taken as true; hidden stars: no Luigi / green star
 * checks */
enum { SS_APPEARSTAR, SS_APPEAR, SS_WAIT, SS_DECIDE, SS_AFTER, SS_CANCEL, SS_CANCELFADE };
enum { ST_APPEAR, ST_NOTPOINTING, ST_POINTING, ST_ENDPOINTING, ST_SELECTED, ST_SELECTEDMOVE, ST_NOTSELECTED };
static const char *const ss_star_pane[7] = {"Star1", "Star2", "Star3", "Star4", "Star5", "Star6", "Star7"};
static const char *const ss_question_pane[2] = {"NewStar1", "NewStar2"};

typedef struct {
    u8 dead, pointing;                  /* _30, mIsPointing */
    int status, scenario, id, rot_time, appear_frame;
    float rot_speed, rot_z, scale, scale_on_select;
    float base[3], pos[3], pos_on_select[3];
    const char *clip;                   /* its BCK (ScenarioAppear / ScenarioHide), NULL none */
    float frame, clip_len;
    Nrv n;
} SsStar;

static struct {
    LytInst *l, *bl;                    /* ScenarioSelect, BackButton */
    Btn back;
    u8 back_clicked;                    /* BackButton _24 */
    SsStar star[7];
    const Model *m_star, *m_empty, *m_sky;  /* SelectPowerStar (BVA frame 0), its empty one (1), the sky */
    Anim *anim;
    char galaxy[32];
    int alive, selected, locked, hold, a4, a8, sky_frame; /* mSelectedScenarioNo, _28, _24, _A4, _A8 */
    float star_top[2], new_pos[2], green_pos[2], mario_pos[2], q_pos[2][2];
    const char *mario_pane;
    u16 coin_txt[8];
    u32 fly;                            /* SE_DM_LV_SENARIO_SEL_FLY */
    Nrv n;
} ss;


static float ease_out(float t) { return sinf(t * 1.5707963f); }
static float rate_of(int step, int n) { float t = n <= 0 ? 1.0f : (float)step / n; return t < 0 ? 0 : t > 1 ? 1 : t; }
static float reaction(float x, float freq, float amp) /* getScaleWithReactionValueZeroToOne */
{
    if (x < 0.5f) return sinf(x * 3.14159265f);
    float t = (x - 0.5f) * 2.0f;
    return 1.0f + (1.0f - cosf(3.14159265f * freq * t)) * (1.0f - t) * amp;
}
static float repeat_deg(float v) { v = fmodf(v, 360.0f); return v < 0 ? v + 360.0f : v; }

static void sst_bck(SsStar *s, const char *clip) /* MultiScene::startBck */
{
    int mode;
    s->clip = clip; s->frame = 0;
    s->clip_len = (float)anim_clip_info(clip, &mode);
}
static int sst_bck_stopped(const SsStar *s) { return !s->clip || s->frame >= s->clip_len; }

static void sst_update_pos(SsStar *s)
{
    float off = sinf(repeat_deg(s->rot_time * 360.0f / 180.0f) * 0.017453293f) * 15.0f; /* cMoveInterval, cMoveDistance */
    s->pos[0] = s->base[0]; s->pos[1] = s->base[1] + off; s->pos[2] = s->base[2];
}

static void sst_setup(SsStar *s, int scenario, int status, const float pos[3], int id)
{
    s->status = status; s->scenario = scenario; s->id = id;
    memcpy(s->base, pos, sizeof s->base);
    s->rot_z = 150.0f * id;
    s->appear_frame = id * 15 + 60;                  /* cAppearDelayFrame, cAppearWaitFrame */
    s->rot_time = id * 15 + (id % 2 ? 0 : 90);
}

static void sst_appear(SsStar *s)
{
    s->scale = 0.9f; s->pointing = 0; s->rot_speed = 0; s->clip = NULL; s->dead = 0;
    sst_update_pos(s);
    s->n.next = -1;
    nrv_set(&s->n, ST_APPEAR);
}

static void sst_try_pointing(SsStar *s)
{
    s->pointing = 1;
    if (nrv_is(&s->n, ST_NOTPOINTING) || nrv_is(&s->n, ST_ENDPOINTING)) nrv_set(&s->n, ST_POINTING);
}

static int sst_appear_end(const SsStar *s) { return !s->dead && !nrv_is(&s->n, ST_APPEAR); }

static void sst_update(SsStar *s)
{
    if (s->dead) return;
    if (s->clip && s->frame < s->clip_len) s->frame += 1.0f;
    switch (nrv_begin(&s->n)) {
    case ST_APPEAR:
        if (s->n.step == s->appear_frame) { sst_bck(s, "selectpowerstar/scenarioappear"); se_start(SE_OJ_SCEN_SEL_STAR_APPEAR, NULL); }
        if (s->n.step > s->appear_frame && sst_bck_stopped(s)) nrv_set(&s->n, ST_NOTPOINTING);
        break;
    case ST_NOTPOINTING:
        if (FIRST(&s->n)) { s->scale = 0.9f; s->rot_speed = 1.5f; }
        break;
    case ST_POINTING:
        if (FIRST(&s->n)) { se_start(SE_SY_CURSOR_1, NULL); s->rot_speed = 3.0f; }
        if (s->n.step < 20) s->scale = 0.9f + (1.5f - 0.9f) * reaction(rate_of(s->n.step, 20), 1.5f, -0.25f);
        else s->scale = 1.5f;
        if (!s->pointing) nrv_set(&s->n, ST_ENDPOINTING); /* tryEndPointing */
        break;
    case ST_ENDPOINTING:
        if (FIRST(&s->n)) s->rot_speed = 1.5f;
        s->scale = 1.5f + (0.9f - 1.5f) * ease_out(rate_of(s->n.step, 10));
        if (s->n.step == 10) nrv_set(&s->n, ST_NOTPOINTING);
        break;
    case ST_SELECTED:
        if (FIRST(&s->n)) { memcpy(s->pos_on_select, s->pos, sizeof s->pos); s->rot_speed = 3.0f; s->scale_on_select = s->scale; }
        s->scale = s->scale_on_select + (1.5f - s->scale_on_select) * rate_of(s->n.step, 40);
        if (s->n.step == 40) nrv_set(&s->n, ST_SELECTEDMOVE);
        break;
    case ST_SELECTEDMOVE:
        if (FIRST(&s->n)) s->scale = 1.5f;
        if (s->n.step == 60) se_start(SE_DM_SENARIO_SEL_ACCEL, NULL);
        if (s->n.step < 60) {
            float t = ease_out(rate_of(s->n.step, 60));
            for (int k = 0; k < 3; k++) s->pos[k] = (1.0f - t) * s->pos_on_select[k];
            s->rot_speed = 3.0f + (6.0f - 3.0f) * t;
        } else {
            s->pos[0] = s->pos[1] = s->pos[2] = 0;
            s->rot_speed = 6.0f + (20.0f - 6.0f) * rate_of(s->n.step - 60, 180 - 60);
        }
        break;
    case ST_NOTSELECTED: {
        int hide = 10 * s->id; /* cHideDelayFrame */
        if (FIRST(&s->n)) s->rot_speed = 1.5f;
        if (s->n.step == hide) { s->rot_speed = 0; s->rot_z = 150.0f * s->id; sst_bck(s, "selectpowerstar/scenariohide"); }
        if (s->n.step > hide && sst_bck_stopped(s)) s->dead = 1;
        break;
    }
    }
    nrv_end(&s->n);
    /* control */
    s->pointing = 0;
    s->rot_z = repeat_deg(s->rot_z + s->rot_speed);
    if (nrv_is(&s->n, ST_NOTPOINTING) || nrv_is(&s->n, ST_POINTING) || nrv_is(&s->n, ST_ENDPOINTING)) { s->rot_time++; sst_update_pos(s); }
}

static void sst_draw(SsStar *s)
{
    if (s->dead || (nrv_is(&s->n, ST_APPEAR) && s->n.step <= s->appear_frame + 1)) return; /* calcViewAndEntry */
    float m[16], rot[3] = {0, 0, s->rot_z}, sc[3] = {s->scale, s->scale, s->scale};
    mat_translate_rot_scale(m, s->pos, rot, sc);
    if (s->clip) { /* its frame as kept (anim_pose steps by speed: 0) */
        anim_start(ss.anim, s->clip, 0);
        ss.anim->speed = 0;
        ss.anim->frame = s->frame < s->clip_len ? s->frame : s->clip_len;
    }
    else anim_bind(ss.anim, ss.m_star->skin);
    anim_pose(ss.anim);
    anim_skin(ss.anim);
    const Model *md = !s->status && ss.m_empty ? ss.m_empty : ss.m_star; /* setBvaFrameAndStop(status 0 ? 1 : 0) */
    render_skinned(md, m, (const float (*)[16])ss.anim->drw);
}

/* the galaxy's ScenarioData: its normal scenarios (no Comet) and stars */
static int ss_normal_num(void)
{
    int n = 0;
    for (int i = 1; i <= gd_scenario_num(ss.galaxy); i++) n += !gd_scenario_comet(ss.galaxy, i)[0];
    return n;
}
static int ss_has(int no) { return gd_has_star(ss.galaxy, no); } /* hasPowerStarInCurrentStage */

static int ss_display_num(int *normal, int *extra) /* calcDisplayScenarioNum */
{
    int nn = ss_normal_num(), hidden = gd_power_star_num(ss.galaxy) - nn, is_new = 0;
    for (*normal = 0; *normal < nn; (*normal)++)
        if (!ss_has(*normal + 1)) { (*normal)++; is_new = 1; break; }
    *extra = 0;
    for (int i = 0; i < hidden; i++) *extra += ss_has(nn + i + 1);
    return is_new;
}

static void ss_appear_star(int id, int scenario, int status) /* appearStar: its pane's place at depth 1000 */
{
    float p[2], w[3];
    lyt_copy_pane_trans(ss.l, ss_star_pane[id], p);
    float focal = 228.0f / tanf(22.5f * 0.017453293f), k = 1000.0f / focal; /* EFB height 456, fovy 45 */
    w[0] = k * (p[0] - 304.0f); w[1] = -k * (p[1] - 228.0f); w[2] = 0; /* the inverse view of eye (0, 0, 1000) */
    sst_setup(&ss.star[id], scenario, status, w, id);
    sst_appear(&ss.star[id]);
}

static void ss_new_anim(const char *anim, float rate)
{
    static const char *const panes[4] = {"New", "NewStarGreen", "NewStar1", "NewStar2"};
    for (int i = 0; i < 4; i++) {
        if (anim) lyt_start_pane(ss.l, panes[i], anim, 0);
        if (rate >= 0) lyt_set_pane_rate(ss.l, panes[i], 0, rate);
    }
}

static const u16 *ss_msg(const char *fmt, const char *a, int n)
{
    char label[64];
    snprintf(label, sizeof label, fmt, a, n);
    return msg_get(label);
}

static void ss_appear(void)
{
    int normal, extra, is_new = ss_display_num(&normal, &extra);
    lyt_start_pane(ss.l, "ScenarioSelect", "Appear", 0);
    lyt_set_pane_frame_stop(ss.l, "ScenarioSelect", 0, 0);
    ss_new_anim("Appear", 0.0f);
    lyt_start_pane(ss.l, "ScenarioFrame", "Wait", 0);
    lyt_start_pane(ss.l, "StarTop", "StarPositionTop", 0);
    lyt_set_pane_frame_stop(ss.l, "StarTop", 0, (float)(normal - 1));
    if (extra > 0) {
        lyt_start_pane(ss.l, "StarDown", "StarPositionDown", 0);
        lyt_set_pane_frame_stop(ss.l, "StarDown", 0, (float)(extra - 1));
        ss.star_top[0] = ss.star_top[1] = 0;
    } else { ss.star_top[0] = 0; ss.star_top[1] = 50.0f; }
    lyt_set_text_rec(ss.l, "Galaxy", ss_msg("GalaxyName_%s", ss.galaxy, 0));
    lyt_show_rec(ss.l, "Scenario", 0);
    lyt_set_text_rec(ss.l, "ShaMario", msg_get("PlayerLeft")); /* setPlayerLeft */
    lyt_set_number_rec(ss.l, "ShaMarioNum", gd_player_left());
    ss.mario_pane = gd_player_left() < 10 ? "MarioPosition01" : "MarioPosition10";
    /* appearAllStar */
    for (int i = 0; i < 7; i++) ss.star[i].dead = 1;
    for (int i = 0; i < normal; i++) ss_appear_star(i, i + 1, !is_new ? 2 : i < normal - 1 ? 1 : 0);
    int nn = ss_normal_num(), power = gd_power_star_num(ss.galaxy), idx = 3;
    for (int j = 0; j < power - nn; j++)
        if (ss_has(nn + j + 1)) ss_appear_star(idx++, nn + j + 1, is_new ? 1 : 2);
    lyt_show_rec(ss.l, "New", 0);
    lyt_show_rec(ss.l, "NewStarGreen", 0);
    for (int i = 0; i < 2; i++) lyt_show_rec(ss.l, ss_question_pane[i], 0);
    if (is_new) {
        lyt_show_rec(ss.l, "New", 1);
        lyt_copy_pane_trans(ss.l, ss_star_pane[normal - 1], ss.new_pos);
    } else {
        int hi = 0;
        for (int no = nn + 1; no <= power && hi < 2; no++) {
            if (!gd_scenario_hidden(ss.galaxy, no) || ss_has(no)) continue;
            int placed = 1; /* getPlacedHiddenStarScenarioNo: the normal scenario whose PowerStarId has it */
            for (int i = 1; i <= nn; i++)
                if (gd_scenario_star_mask(ss.galaxy, i) & (1 << (no - 1))) { placed = i; break; }
            lyt_show_rec(ss.l, ss_question_pane[hi], 1);
            lyt_copy_pane_trans(ss.l, ss_star_pane[placed - 1], ss.q_pos[hi]);
            hi++;
        }
    }
    lyt_start_pane(ss.l, "BestScore", "SelectIn", 0);
    lyt_start_pane(ss.l, "BestTime", "SelectIn", 0);
    lyt_set_pane_frame_stop(ss.l, "BestScore", 0, 0);
    lyt_set_pane_frame_stop(ss.l, "BestTime", 0, 0);
    ss.sky_frame = 0;
    ss.selected = -1; ss.hold = 0; ss.locked = 0; ss.a4 = 0; ss.a8 = -1;
    ss.n.next = -1;
    nrv_set(&ss.n, SS_APPEARSTAR);
}

static SsStar *ss_selected_star(void)
{
    for (int i = 0; i < 7; i++)
        if (!ss.star[i].dead && ss.star[i].scenario == ss.selected) return &ss.star[i];
    return NULL;
}

static int ss_pointing_star(int i) /* isStarPointerPointingTarget: the circle of radius 60 round its pane */
{
    float x, y, cx, cy;
    if (!game_cursor(&x, &y) || !lyt_pane_center(ss.l, ss_star_pane[i], &cx, &cy)) return 0;
    float dx = (x - cx) * (608.0f / 640.0f), dy = (y - cy) * (456.0f / 448.0f); /* layout units */
    return dx * dx + dy * dy < 60.0f * 60.0f;
}

static void ss_update_selected(void)
{
    if (nrv_is(&ss.n, SS_WAIT))
        for (int i = 0; i < 7; i++)
            if (!ss.star[i].dead && ss_pointing_star(i)) { ss.selected = ss.star[i].scenario; ss.hold = 0; return; }
    if (ss.hold < 0) ss.hold++;
    if (ss.hold >= 0) ss.selected = -1;
}

static void ss_fade_in_text(void)
{
    ss.a4 = 0; ss.a8 = ss.selected;
    lyt_set_text_rec(ss.l, "Scenario", ss_msg("ScenarioName_%s%d", ss.galaxy, ss.selected));
    lyt_show_rec(ss.l, "Scenario", 1);
    lyt_start_pane(ss.l, "Scenario", "SelectIn", 0);
    lyt_start_pane(ss.l, "BestScore", "SelectIn", 0);
    char b[8];
    snprintf(b, sizeof b, "%03d", gd_max_coins(ss.galaxy, ss.selected));
    for (int i = 0; i < 8; i++) ss.coin_txt[i] = (u8)b[i];
    lyt_set_text_rec(ss.l, "ShaCoinNum", ss.coin_txt);
    lyt_start_pane(ss.l, "BestTime", "SelectIn", 0);
    lyt_set_pane_frame_stop(ss.l, "BestTime", 0, 0);
    lyt_set_text_rec(ss.l, "ShaTime", NULL);
}

static void ss_update_text(void)
{
    int end_step = nrv_is(&ss.n, SS_AFTER) && ss.n.step >= 60; /* cSelectedEndAnimStartStep */
    if (!lyt_pane_shown(ss.l, "Scenario") || (ss.selected > 0 && ss.selected != ss.a8)) {
        if (!end_step && ss.selected > 0) ss_fade_in_text();
    } else if (ss.a4 == 0) {
        if (lyt_pane_stopped(ss.l, "Scenario", 0) && ss.selected <= 0) { /* fadeOutText */
            ss.a4 = 1;
            lyt_start_pane(ss.l, "Scenario", "SelectOut", 0);
            lyt_start_pane(ss.l, "BestScore", "SelectOut", 0);
        }
    } else if (lyt_pane_stopped(ss.l, "Scenario", 0))
        lyt_show_rec(ss.l, "Scenario", 0);
}

static int ss_try_select(void)
{
    if (!pad_pressed(PAD_CROSS)) return 0; /* testDPDMenuPadDecideTrigger */
    if (ss.selected > 0) return 1;
    float x, y;
    if (!game_cursor(&x, &y)) /* the pointer off the screen: the first star not taken */
        for (int i = 0; i < 7; i++)
            if (!ss.star[i].dead && !ss.star[i].status) { ss.selected = ss.star[i].scenario; return 1; }
    return 0;
}

static void ss_back_update(void) /* BackButton::control */
{
    if (btn_pointing_trigger(&ss.back)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
    if (btn_try_select(&ss.back)) {
        if (!ss.back_clicked) se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL);
        ss.back_clicked = 1;
    }
    btn_update(&ss.back);
    lyt_update(ss.bl);
}

static int ss_try_cancel(void)
{
    if (ss.back_clicked) { nrv_set(&ss.n, SS_CANCEL); return 1; }
    if (pad_pressed(PAD_CIRCLE) || pad_pressed(PAD_TRIANGLE)) { /* B (Circle / Triangle) */
        se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL);
        btn_disappear(&ss.back);
        nrv_set(&ss.n, SS_CANCEL);
        return 1;
    }
    return 0;
}

static void ss_update(void)
{
    ss.sky_frame++;
    switch (nrv_begin(&ss.n)) {
    case SS_APPEARSTAR: {
        ss.fly = se_level(SE_DM_LV_SENARIO_SEL_FLY, NULL, ss.fly);
        int all = 1;
        for (int i = 0; i < 7; i++) all &= ss.star[i].dead || sst_appear_end(&ss.star[i]);
        if (all) nrv_set(&ss.n, SS_APPEAR);
        break;
    }
    case SS_APPEAR:
        if (FIRST(&ss.n)) { lyt_set_pane_rate(ss.l, "ScenarioSelect", 0, 1.0f); ss_new_anim(NULL, 1.0f); }
        ss.fly = se_level(SE_DM_LV_SENARIO_SEL_FLY, NULL, ss.fly);
        if (lyt_pane_stopped(ss.l, "ScenarioSelect", 0)) nrv_set(&ss.n, SS_WAIT);
        break;
    case SS_WAIT:
        if (FIRST(&ss.n)) { ss_new_anim("NewWait", -1); ss.back_clicked = 0; btn_appear(&ss.back); }
        ss_update_selected();
        ss_update_text();
        ss.fly = se_level(SE_DM_LV_SENARIO_SEL_FLY, NULL, ss.fly);
        if (ss_try_cancel()) break;
        if (ss_try_select()) {
            ss.locked = 1;
            se_start(SE_SY_DECIDE_1, NULL);
            btn_disappear(&ss.back);
            nrv_set(&ss.n, SS_DECIDE);
        }
        break;
    case SS_DECIDE:
        if (FIRST(&ss.n)) {
            SsStar *sel = ss_selected_star();
            for (int i = 0; i < 7; i++)
                if (!ss.star[i].dead) nrv_set(&ss.star[i].n, &ss.star[i] == sel ? ST_SELECTED : ST_NOTSELECTED);
            ss_new_anim("End", -1);
        }
        ss_update_text();
        ss.fly = se_level(SE_DM_LV_SENARIO_SEL_FLY, NULL, ss.fly);
        if (ss.n.step == 40) nrv_set(&ss.n, SS_AFTER); /* cDecideFrame */
        break;
    case SS_AFTER:
        if (ss.n.step == 60) lyt_start_pane(ss.l, "ScenarioFrame", "End", 0);
        ss_update_text();
        ss.fly = se_level(SE_DM_LV_SENARIO_SEL_FLY, NULL, ss.fly);
        if (ss.n.step == 180) wipe_start(30, 1, 0xFFFFFF); /* closeSystemWipeWhiteFade(cSelectedFadeOutFrame) */
        if (ss.n.step == 211) { /* isReadyToDisappear: the galaxy, opened by a white fade (cSelectedFadeInFrame) */
            seq_set_open_wipe(180, 0xFFFFFF);
            seq_galaxy_move(SEQ_MOVE, ss.galaxy, ss.selected, -1, 0);
        }
        break;
    case SS_CANCEL:
        if (FIRST(&ss.n)) ss.selected = -1;
        ss_update_text();
        if (btn_hidden(&ss.back)) { wipe_start(90, 1, 0); nrv_set(&ss.n, SS_CANCELFADE); }
        break;
    case SS_CANCELFADE: /* ponytail: back to the stage left, still loaded (the dome's selector not ported yet) */
        if (!wipe_active()) { ss.alive = 0; wipe_start(30, 0, 0); }
        break;
    }
    nrv_end(&ss.n);
    /* control */
    if (!ss.locked && ss.selected > 0 && ss_selected_star()) sst_try_pointing(ss_selected_star());
    lyt_copy_pane_trans(ss.l, ss.mario_pane, ss.mario_pos);
    lyt_update(ss.l);
    for (int i = 0; i < 7; i++) sst_update(&ss.star[i]);
    ss_back_update();
}

void menu_scenario_select(const char *galaxy)
{
    if (!ss.l || !ss.m_star || !ss.anim) { /* no layout: straight in at its first scenario */
        seq_galaxy_move(SEQ_MOVE, galaxy, 1, -1, 0);
        return;
    }
    fade.frames = 0;
    snprintf(ss.galaxy, sizeof ss.galaxy, "%s", galaxy);
    ss.alive = 1;
    ss_appear();
    printf("scenario select: %s\n", ss.galaxy);
}
int menu_scenario_selecting(void) { return ss.alive; }
int menu_test_point_scenario(const char *pane) /* tests: the pointer on a pane of the scenario select */
{
    float x, y;
    if (!ss.alive || !lyt_pane_center(ss.l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}

static void ss_init(const Scene *s)
{
    memset(&ss, 0, sizeof ss);
    ss.l = lyt_new("ScenarioSelect");
    ss.bl = lyt_new("BackButton");
    ss.m_star = scene_model(s, "SelectPowerStar");
    ss.m_empty = scene_model(s, "SelectPowerStarEmpty");
    ss.m_sky = scene_model(s, "ScenarioSelectSky");
    if (!ss.l || !ss.bl || !ss.m_star || !ss.m_star->skin) { ss.l = NULL; return; }
    ss.anim = mem_alloc(MEM_SCENE, sizeof(Anim));
    anim_bind(ss.anim, ss.m_star->skin);
    btn_init(&ss.back, ss.bl, "Back", "BoxButton", 0, 1);
    lyt_follow(ss.l, "StarTop", ss.star_top, 1);
    lyt_follow(ss.l, "New", ss.new_pos, 0);
    lyt_follow(ss.l, "NewStarGreen", ss.green_pos, 0);
    lyt_follow(ss.l, "Mario", ss.mario_pos, 0);
    for (int i = 0; i < 2; i++) lyt_follow(ss.l, ss_question_pane[i], ss.q_pos[i], 0);
}

static void wipe_draw_quad(void)
{
    if (!wipe.frames) return;
    float t = wipe.t >= wipe.frames ? 1.0f : (float)wipe.t / wipe.frames;
    int a = (int)(128 * (wipe.close ? t : 1.0f - t));
    if (a <= 0) return;
    Quad q = {0, 0, 640, 448, 0, 0, 0, 0, (u32)a << 24 | wipe.rgb, NO_TEX, 0, 0, 0};
    render_quads(&q, 1);
}

int menu_draw_screen(void)
{
    if (!ss.alive) return 0;
    float eye[3] = {0, 0, 1000.0f}, target[3] = {0, 0, 0}, up[3] = {0, 1.0f, 0};
    render_set_fovy(45.0f);
    render_set_shake(0, 0);
    render_begin(eye, target, up);
    if (ss.m_sky) { /* cSkyTrans, cSkyScale; its BTK from the select's start */
        float m[16], p[3] = {0, 0, -10000.0f}, r[3] = {0, 0, 0}, k[3] = {10.0f, 10.0f, 10.0f};
        for (u32 g = 0; g < ss.m_sky->vu_ngroups; g++)
            if (ss.m_sky->vu[g].btk) anim_btk(ss.m_sky->vu[g].btk, (float)ss.sky_frame, ss.m_sky->vu[g].uvm);
        mat_translate_rot_scale(m, p, r, k);
        render_model(ss.m_sky, m, 0x80, 0x80, 0x80);
    }
    for (int i = 0; i < 7; i++) sst_draw(&ss.star[i]);
    render_world_done();
    lyt_draw(ss.l);
    lyt_draw(ss.bl);
    wipe_draw_quad();
    fade_draw();
    return 1;
}

static void wipe_update(void)
{
    if (!wipe.frames) return;
    if (wipe.t < wipe.frames) wipe.t++;
    else if (!wipe.close) wipe.frames = 0;
}

void menu_draw(void)
{
    wipe_draw_quad();
    fade_draw();
    if (!pm.alive) return;
    render_world_done(); /* the scene under the menu (LayoutOnPause draws after everything) */
    lyt_draw(pm.l);
    if (pm.win.alive) lyt_draw(pm.win.l);

}
