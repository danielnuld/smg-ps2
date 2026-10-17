#include "menu.h"

#include "game.h"
#include "gamedata.h"
#include "sequence.h"
#include "lyt.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "se.h"
#include "se_ids.h"

#include <stdio.h>
#include <string.h>

static void fade_update(void);

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

void menu_draw(void)
{
    fade_draw();
    if (!pm.alive) return;
    render_world_done(); /* the scene under the menu (LayoutOnPause draws after everything) */
    lyt_draw(pm.l);
    if (pm.win.alive) lyt_draw(pm.win.l);

}
