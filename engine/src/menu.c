#include "menu.h"

#include "anim.h"
#include "game.h"
#include "gamedata.h"
#include "sequence.h"
#include "lyt.h"
#include "mc.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "se.h"
#include "se_ids.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void fade_update(void);
static int sv_prompt; /* frames to the save prompt a stage clear left for this scene, 0 none */
static void fade_draw(void);
static void ss_init(const Scene *s);
static void dmu_init(void);
static void dmu_update(void);
static void mlay_init(void);
static void fs_init(const Scene *s);
static void fs_update(void);
static void fs_draw(void);
static void gm_init(void);
static void gm_update(void);
static void gm_draw(void);
static int gm_screen(void);
int menu_galaxy_map_active(void);
int menu_file_selecting(void);
static void mlay_update(void);
static void mlay_draw(void);
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

static void si_disappear(SysInfo *w) { if (w->alive) nrv_set(&w->n, SI_DISAPPEAR); } /* SysInfoWindow::disappear */

/* --- SaveDataHandleSequence (startSave): "save?" (System_Save00, or System_Save07 save and quit), the save with
 * System_Save01 for at least 120 frames, System_Save02; with confirm-remind a No asks ConfirmEndGame. The current
 * game goes into its file (gd_file_save) and the files to the memory card (main.c, mc.c) --------------------------- */
enum { SV_OFF, SV_CONFIRM, SV_SAVE, SV_WINDOWOUT, SV_DONE, SV_REMIND };
static struct { Nrv n; SysInfo win; int remind, quit, result; u32 lv; } sv;
static int save_requested;
void menu_save_request(void) { save_requested = 1; }
int menu_take_save_request(void) { int r = save_requested; save_requested = 0; return r; }
void menu_save_start(int confirm, int remind, int quit)
{
    sv.remind = remind; sv.quit = quit; sv.result = 0;
    sv.n.next = -1;
    nrv_set(&sv.n, confirm ? SV_CONFIRM : SV_SAVE);
}
int menu_saving(void) { return sv.n.cur != SV_OFF || sv.n.next > SV_OFF; }
int menu_save_result(void) { return sv.result; } /* 2 saved (or quit without), 3 not saved */
static void sv_update(void)
{
    if (!menu_saving()) return;
    switch (nrv_begin(&sv.n)) {
    case SV_CONFIRM:
        if (FIRST(&sv.n)) si_appear(&sv.win, sv.quit ? "System_Save07" : "System_Save00", SI_YESNO);
        if (!sv.win.alive && sv.n.step > 0) {
            if (yn_yes(&sv.win.yn)) nrv_set(&sv.n, SV_SAVE);
            else if (sv.remind) nrv_set(&sv.n, SV_REMIND);
            else { sv.result = 3; nrv_set(&sv.n, SV_OFF); }
        }
        break;
    case SV_SAVE: /* trySave */
        if (FIRST(&sv.n)) { gd_file_save(); menu_save_request(); si_appear(&sv.win, "System_Save01", SI_BLOCKING); }
        if (sv.n.step > 20) sv.lv = se_level(SE_SY_LV_SAVING, NULL, sv.lv);
        if (sv.n.step > 120 && !mc_busy()) nrv_set(&sv.n, SV_WINDOWOUT);
        break;
    case SV_WINDOWOUT:
        if (FIRST(&sv.n)) si_disappear(&sv.win);
        if (!sv.win.alive) nrv_set(&sv.n, SV_DONE);
        break;
    case SV_DONE: /* tryProcessDoneKeyWait */
        if (FIRST(&sv.n)) si_appear(&sv.win, "System_Save02", SI_KEY);
        if (!sv.win.alive && sv.n.step > 0) { sv.result = 2; nrv_set(&sv.n, SV_OFF); }
        break;
    case SV_REMIND:
        if (FIRST(&sv.n)) si_appear(&sv.win, "ConfirmEndGame", SI_YESNO);
        if (!sv.win.alive && sv.n.step > 0) {
            if (yn_yes(&sv.win.yn)) { sv.result = 2; nrv_set(&sv.n, SV_OFF); }
            else nrv_set(&sv.n, SV_CONFIRM);
        }
        break;
    }
    nrv_end(&sv.n);
    si_update(&sv.win);
}

/* --- StageResultInformer ("ResultWindow") after the stage clear's return: the star got (System_Result000 / 003 with
 * the galaxy and scenario names, the star count counted up after 85 frames), a new coin record (System_Result006),
 * the galaxy's star pieces counted into the stock (System_Result004); X goes on. ponytail: no galaxy complete / new
 * galaxy / picture book / Tico galaxy information windows, no IconAButton, no effects or BGM */
enum { SR_OFF, SR_APPEARSTAR, SR_WAITCOUNT, SR_COUNT, SR_DISPLAY, SR_DISAPPEAR, SR_APPEARSCORE, SR_APPEARPIECE, SR_DISPLAYPIECE,
       SR_WAITPIECE };
static struct { LytInst *l; Nrv n; int kind, pieces, pressed, alive, delay; SeqResult r; u16 text[160]; } sr;

/* the message with its string arguments (group 7 tags: their last parameter the argument's index) put in */
static void msg_with_args(u16 *out, int cap, const u16 *src, const u16 *const *args, int nargs)
{
    int n = 0;
    while (src && *src && n < cap - 1) {
        if (*src == 0x1A) {
            int len = 4 + (src[3] + 1) / 2;
            if (src[1] == 7 || src[1] == 6) { /* a string or a number (given as its string) */
                u16 last = src[len - 1];
                int idx = (last >> 8 | (last & 0xFF) << 8);
                for (const u16 *a = idx < nargs ? args[idx] : NULL; a && *a && n < cap - 1; a++) out[n++] = *a;
            } else
                for (int k = 0; k < len && n < cap - 1; k++) out[n++] = src[k];
            src += len;
            continue;
        }
        out[n++] = *src++;
    }
    out[n] = 0;
}

static void sr_counters(int star, int score, int piece) /* setCounterPaneShowHide */
{
    lyt_show_pane(sr.l, "TotalStar", star);
    lyt_show_pane(sr.l, "BestScore", score);
    lyt_show_pane(sr.l, "TotalPiece", piece);
}
static void sr_result_text(const u16 *s)
{
    lyt_set_text_rec(sr.l, "Result", s);
    lyt_start(sr.l, "Line", 1); /* setAnimFrameAndStopAdjustTextHeight */
    lyt_set_frame(sr.l, 1, lyt_text_height(sr.l, "Result"));
    lyt_set_rate(sr.l, 1, 0);
}
static int sr_decide(void) { int d = pad_pressed(PAD_CROSS) || sr.pressed; if (d) { sr.pressed = 0; se_start(SE_SY_TALK_OK, NULL); } return d; }
static void sr_next(void) /* decideNextNerve */
{
    if (sr.kind < 1 && sr.r.coins > sr.r.prev_max_coin) { sr.kind = 1; nrv_set(&sr.n, SR_APPEARSCORE); }
    else if (sr.kind < 2 && sr.r.pieces > 0) { sr.kind = 2; nrv_set(&sr.n, SR_APPEARPIECE); }
    else { sr.alive = 0; nrv_set(&sr.n, SR_OFF); }
}
static void sr_update(void)
{
    char label[64];
    switch (nrv_begin(&sr.n)) {
    case SR_APPEARSTAR:
        if (FIRST(&sr.n)) {
            const u16 *args[2];
            snprintf(label, sizeof label, "GalaxyName_%s", sr.r.stage);
            args[0] = msg_get(label);
            snprintf(label, sizeof label, "ScenarioName_%s%d", sr.r.stage, sr.r.star);
            args[1] = msg_get(label);
            msg_with_args(sr.text, 160, msg_get(gd_scenario_hidden(sr.r.stage, sr.r.star) ? "System_Result003" : "System_Result000"), args, 2);
            sr_counters(1, 0, 0);
            lyt_start(sr.l, "Appear", 0);
            sr_result_text(sr.text);
            lyt_set_number_rec(sr.l, "ShaStarNum", sr.r.prev_stars);
            snprintf(label, sizeof label, "%d", sr.r.coins);
            lyt_set_number_rec(sr.l, "ShaCoinNum", sr.r.coins);
            sr.kind = 0;
        }
        if (lyt_stopped(sr.l, 0)) nrv_set(&sr.n, SR_WAITCOUNT);
        break;
    case SR_WAITCOUNT: /* tryWaitIntervalBeforeKeyWait(sWaitBeforeCountUpPowerStar 85) */
        if (pad_pressed(PAD_CROSS)) sr.pressed = 1;
        if (sr.n.step >= 85) nrv_set(&sr.n, sr.r.prev_stars != sr.r.after_stars ? SR_COUNT : SR_DISPLAY);
        break;
    case SR_COUNT:
        if (FIRST(&sr.n)) { lyt_set_number_rec(sr.l, "ShaStarNum", sr.r.after_stars); lyt_start(sr.l, "Flash", 0); se_start(SE_SY_RESULT_POW_STAR_INC, NULL); }
        if (pad_pressed(PAD_CROSS)) sr.pressed = 1;
        if (lyt_stopped(sr.l, 0)) nrv_set(&sr.n, SR_DISPLAY);
        break;
    case SR_DISPLAY:
        if (sr_decide()) nrv_set(&sr.n, SR_DISAPPEAR);
        break;
    case SR_DISAPPEAR:
        if (FIRST(&sr.n)) lyt_start(sr.l, "End", 0);
        if (lyt_stopped(sr.l, 0)) sr_next();
        break;
    case SR_APPEARSCORE: /* AppearUpdateBestScore */
        if (FIRST(&sr.n)) { sr_counters(0, 1, 0); lyt_show_pane(sr.l, "Coin", 1); lyt_start(sr.l, "Appear", 0); sr_result_text(msg_get("System_Result006")); }
        if (lyt_stopped(sr.l, 0)) nrv_set(&sr.n, SR_DISPLAY);
        break;
    case SR_APPEARPIECE:
        if (FIRST(&sr.n)) {
            sr_counters(0, 0, 1); lyt_start(sr.l, "Appear", 0); sr_result_text(msg_get("System_Result004"));
            sr.pieces = sr.r.pieces;
            lyt_set_number_rec(sr.l, "ShaTPieceLNum", sr.pieces);
            lyt_set_number_rec(sr.l, "ShaTPieceRNum", gd_stocked_star_pieces());
        }
        if (lyt_stopped(sr.l, 0)) nrv_set(&sr.n, SR_DISPLAYPIECE);
        break;
    case SR_DISPLAYPIECE: { /* one a frame into the stock */
        int done = 0;
        if (sr.pieces > 0) {
            sr.pieces--;
            gd_add_stocked_star_pieces(1);
            if (sr.pieces > 0) { if (sr.pieces % 2 == 0) se_start(SE_SY_STAR_PIECE_SUM_UP, NULL); }
            else se_start(SE_SY_STAR_PIECE_SUM_UP_END, NULL);
        }
        if (sr.pieces == 0) done = 1;
        lyt_set_number_rec(sr.l, "ShaTPieceLNum", sr.pieces);
        lyt_set_number_rec(sr.l, "ShaTPieceRNum", gd_stocked_star_pieces());
        if (lyt_stopped(sr.l, 0) && !done) lyt_start(sr.l, "Flash", 0);
        if (pad_pressed(PAD_CROSS)) { se_start(SE_SY_TALK_OK, NULL); nrv_set(&sr.n, done ? SR_DISAPPEAR : SR_WAITPIECE); }
        break;
    }
    case SR_WAITPIECE:
        if (FIRST(&sr.n)) {
            gd_add_stocked_star_pieces(sr.pieces); sr.pieces = 0;
            se_start(SE_SY_STAR_PIECE_SUM_UP_END, NULL);
            lyt_set_number_rec(sr.l, "ShaTPieceLNum", 0);
            lyt_set_number_rec(sr.l, "ShaTPieceRNum", gd_stocked_star_pieces());
        }
        if (sr.n.step == 45) nrv_set(&sr.n, SR_DISAPPEAR); /* sWaitDisplayStarPieceCount */
        break;
    }
    nrv_end(&sr.n);
    lyt_update(sr.l);
}
int menu_result_showing(void) { return sr.alive || sr.delay; }

/* --- PauseMenu ------------------------------------------------------------------------------------------------- */
enum { PM_SELECTING, PM_DECIDED, PM_DISAPPEAR, PM_CONFIRM, PM_SAVE };
static struct {
    LytInst *l;
    Btn up, down;
    SysInfo win;
    u8 alive, back_astro;
    int fade; /* closeSystemWipeFadeWithCaptureScreen: frames into the fade before the scene changes */
    Nrv n;
    u16 piece_msg[3];
    float coin_pos[2], piece_pos[2], title_off[2]; /* setFollowPos: Coin, Piece (replace), ShaStageTitle (add) */
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
    lyt_follow(pm.l, "Coin", pm.coin_pos, 0);
    lyt_follow(pm.l, "Piece", pm.piece_pos, 0);
    if (pm_hide_scenario()) { pm.title_off[0] = 0; pm.title_off[1] = 39.0f; lyt_follow(pm.l, "ShaStageTitle", pm.title_off, 1); } /* mStageTitleOffset */
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
    { /* the counters at the places their digits need (CPosition1..3, PPosition1..4) */
        int c = game_coins(), sp = game_starpieces();
        lyt_copy_pane_trans(pm.l, c >= 100 ? "CPosition3" : c >= 10 ? "CPosition2" : "CPosition1", pm.coin_pos);
        lyt_copy_pane_trans(pm.l, sp >= 1000 ? "PPosition4" : sp >= 100 ? "PPosition3" : sp >= 10 ? "PPosition2" : "PPosition1", pm.piece_pos);
    }
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
        } else if (pm.down.selected && btn_decided_wait(&pm.down)) nrv_set(&pm.n, pm_invalid_back() ? PM_SAVE : PM_CONFIRM);
        break;
    case PM_DISAPPEAR:
        if (FIRST(&pm.n)) { pm_anim_without_button("End"); btn_disappear(&pm.up); btn_disappear(&pm.down); }
        if (pm_stopped_without_button() && btn_hidden(&pm.up) && btn_hidden(&pm.down)) pm.alive = 0;
        break;
    case PM_SAVE: /* exeGameDataSave: startGameDataSaveSequence(false, true) */
        if (FIRST(&pm.n)) menu_save_start(1, 0, 1);
        else if (!menu_saving()) nrv_set(&pm.n, PM_CONFIRM);
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
    si_init(&sv.win);
    sv_prompt = seq_take_save_prompt() ? 90 : 0;
    memset(&sr, 0, sizeof sr);
    sr.l = lyt_new("ResultWindow");
    if (sv_prompt && seq_take_result(&sr.r)) sr.delay = 60;
    dmu_init();
    mlay_init();
    fs_init(s);
    gm_init();
    u32 rgb;
    int open = seq_take_open_wipe(&rgb); /* the wipe the last scene closed opens on this one */
    if (open) wipe_start(open, 0, rgb);
}

int menu_paused(void) { return pm.alive; }

int menu_test_point(const char *pane)
{
    float x, y;
    LytInst *l = sv.win.alive ? sv.win.l : pm.win.alive && lyt_pane(pm.win.l, pane) >= 0 ? pm.win.l : pm.l;
    if (!lyt_pane_center(l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}

int menu_update(void)
{
    fade_update();
    wipe_update();
    dmu_update();
    mlay_update();
    if (sr.delay && --sr.delay == 0 && sr.l) { /* the clear's demo sequence: StageResultInformer, then the save */
        sr.alive = 1; sr.n.next = -1; nrv_set(&sr.n, SR_APPEARSTAR);
    }
    if (sr.alive) { sr_update(); if (!sr.alive) sv_prompt = sv_prompt ? 1 : 0; return 1; }
    if (sv_prompt && !sr.delay && --sv_prompt == 0) menu_save_start(1, 0, 0); /* StorySequenceExecutor::tryStartSave */
    if (menu_saving()) { game_cursor_menu(); sv_update(); if (!pm.alive) return 1; }
    if (menu_scenario_selecting()) { game_cursor_menu(); ss_update(); return 1; }
    if (menu_file_selecting()) { game_cursor_menu(); fs_update(); return 1; }
    if (menu_galaxy_map_active()) { game_cursor_menu(); gm_update(); return 1; } /* GameScene::exeGalaxyMap */
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
/* a fade to black kept until menu_fade_open (no move after it: a movie's stand-in) and its open */
void menu_fade_hold(int frames) { wipe_start(frames, 1, 0); }
void menu_fade_open(int frames) { wipe_start(frames, 0, 0); }

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
    case SS_CANCELFADE: /* the stage left again (ScenarioSelectScene's cancel: back to the dome), a fade open there */
        if (!wipe_active() && ss.n.step == 0) {
            static char again[48];
            snprintf(again, sizeof again, "%s", game_stage_name);
            seq_set_open_wipe(30, 0);
            game_request_scene(again);
        }
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
    if (gm_screen()) { /* deactivateGameSceneDraw3D */
        float eye[3] = {0, 0, 1000.0f}, target[3] = {0, 0, 0}, up[3] = {0, 1.0f, 0};
        render_begin(eye, target, up);
        gm_draw();
        wipe_draw_quad();
        return 1;
    }
    if (menu_file_selecting()) { fs_draw(); return 1; }
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

/* --- AstroDomeGalaxySelector's layouts (game.c's dome select drives them): GalaxySelectInfo, GalaxySelectBackButton,
 * GalaxyConfirmLayout, GalaxyNamePlate (dome.bin, GalaxyInfo with the galaxies' pictures). ponytail: no IconComet /
 * comet window, no green / red star icons (isPowerStarGreen / Red tables), plates not sorted by depth, no
 * GalaxyNamePlateNew effect */
enum { GI_WAIT, GI_DISAPPEAR };
enum { GB_APPEAR, GB_WAIT, GB_DISAPPEAR, GB_DECIDE, GB_DEAD };
enum { GC_SELECTING, GC_SELECTEDBACK, GC_SELECTED };
enum { NP_APPEARREADY, NP_APPEAR, NP_WAIT };
#define DOME_PLATES 8
typedef struct { LytInst *l; u8 alive, shown, last_immediate, type, layout_shown; Nrv n; } DomePlate;
static struct {
    u8 on;
    LytInst *info, *back_l, *conf, *conf_back_l;
    /* GalaxySelectInfo */
    u8 info_alive, info_shown; int info_frame, info_dir; char info_name[32]; Nrv info_n;
    u16 star_icon[48], star_base[48];
    /* GalaxySelectBackButton */
    Btn back; u8 back_alive; Nrv back_n;
    /* GalaxyConfirmLayout and its BackButton */
    Btn start, conf_back; u8 conf_alive, conf_back_clicked; Nrv conf_n;
    /* GalaxyNamePlate */
    DomePlate plate[DOME_PLATES];
} dmu;

static u16 *pic_add(u16 *p, int code) { p[0] = 0x1A; p[1] = 3; p[2] = (u16)(code - '0'); p[3] = 0; p[4] = 0; return p + 4; } /* addPictureFontCode */

static void dmu_star_add(u16 **base, u16 **icon, const char *g, int no) /* addPictureFontStar */
{
    int code = gd_is_grand_star(g, no) ? 'K' : gd_scenario_comet(g, no)[0] ? 'e' : '7';
    *base = pic_add(*base, code);
    *icon = pic_add(*icon, 'R');
}

static void dmu_info_fill(LytInst *l, const char *g, int state) /* GalaxyInfoLayoutSetter::updateInfo */
{
    static const char *const unknown[3] = {"GalaxyInfo_Hatena", "GalaxyInfo_Unknown", "GalaxyInfo_New"};
    char label[64];
    if (state < 3) lyt_set_text_rec(l, "Unknown", msg_get(unknown[state]));
    else { snprintf(label, sizeof label, "GalaxyName_%s", g); lyt_set_text_rec(l, "GalaxyName", msg_get(label)); }
    if (state == 3) lyt_set_text_rec(l, "Unknown", NULL); else lyt_set_text_rec(l, "GalaxyName", NULL);
    if (state != 3) {
        lyt_set_text_rec(l, "StarIcon", NULL);
        lyt_set_text_rec(l, "TxtStarBase", NULL);
        lyt_show_rec(l, "Complete", 0);
    } else { /* setTextBoxStarNum: its buffers crossed as Petari has them (the base string into StarIcon) */
        u16 *base = dmu.star_icon, *icon = dmu.star_base;
        int nn = 0, power = gd_power_star_num(g), complete = 1, normal_done = 1;
        dmu.star_icon[0] = dmu.star_base[0] = 0;
        for (int i = 1; i <= gd_scenario_num(g); i++) nn += !gd_scenario_comet(g, i)[0];
        for (int i = 1; i <= power; i++) complete &= gd_has_star(g, i) != 0;
        for (int i = 1; i <= nn; i++) normal_done &= gd_has_star(g, i) != 0;
        for (int i = 1; i <= nn && base < dmu.star_icon + 40; i++)
            if (gd_has_star(g, i)) dmu_star_add(&base, &icon, g, i);
            else { base = pic_add(base, 'R'); icon = pic_add(icon, '7'); }
        lyt_show_rec(l, "Complete", complete);
        for (int i = nn + 1; i <= power && base < dmu.star_icon + 40; i++) {
            if (complete || gd_has_star(g, i)) { dmu_star_add(&base, &icon, g, i); continue; }
            if (!normal_done) continue;
            if (gd_scenario_comet(g, i)[0]) { base = pic_add(base, 'R'); icon = pic_add(icon, 'e'); }
            else { base = pic_add(base, 'Z'); icon = pic_add(icon, '7'); } /* ponytail: no Luigi / comet-appear checks */
        }
        lyt_set_text_rec(l, "StarIcon", dmu.star_icon);
        lyt_set_text_rec(l, "TxtStarBase", dmu.star_base);
    }
    { /* replaceTexture: "<galaxy>.bti" of GalaxyInfoTexture (its material in dome.bin), the unknown one before open */
        char mn[24];
        snprintf(mn, sizeof mn, "%.20s", state == 3 ? g : "GalaxyInfoTextureUnknown");
        lyt_replace_pane_texture(l, "PicDummy", lyt_material_texture(l, mn));
    }
    lyt_set_pane_frame_stop(l, "CometWindow", 1, 0); /* hideCometWindow */
}
static void dmu_info_update(const char *g, int state) { dmu_info_fill(dmu.info, g, state); }

void menu_dome_info(const char *g, int state) /* GalaxySelectInfo::show */
{
    if (!dmu.info) return;
    dmu.info_shown = 1;
    if (dmu.info_alive && !strcmp(dmu.info_name, g)) return;
    dmu_info_update(g, state);
    snprintf(dmu.info_name, sizeof dmu.info_name, "%s", g);
    dmu.info_dir = 1;
    dmu.info_n.next = -1;
    nrv_set(&dmu.info_n, GI_WAIT);
    lyt_start(dmu.info, "Appear", 0);
    dmu.info_alive = 1;
}

static void dmu_info_movement(void)
{
    if (!dmu.info_alive) return;
    switch (nrv_begin(&dmu.info_n)) {
    case GI_WAIT: if (!dmu.info_shown) { dmu.info_dir = -1; nrv_set(&dmu.info_n, GI_DISAPPEAR); } break;
    case GI_DISAPPEAR: if (!dmu.info_frame) dmu.info_alive = 0; break;
    }
    nrv_end(&dmu.info_n);
    /* control: ValueControl(cFadeinoutFrame 5) */
    dmu.info_shown = 0;
    dmu.info_frame += dmu.info_dir;
    dmu.info_frame = dmu.info_frame < 0 ? 0 : dmu.info_frame > 5 ? 5 : dmu.info_frame;
    if (!dmu.info_frame) dmu.info_alive = 0;
    lyt_set_alpha(dmu.info, dmu.info_frame / 5.0f);
    lyt_update(dmu.info);
}

/* GalaxySelectBackButton: its pointing driven from outside (_23) */
void menu_dome_back(int op)
{
    if (!dmu.back_l) return;
    if (op == 0) { dmu.back_alive = 1; dmu.back_n.next = -1; nrv_set(&dmu.back_n, GB_APPEAR); }
    else if (op == 1) nrv_set(&dmu.back_n, GB_DISAPPEAR);
    else if (op == 2) nrv_set(&dmu.back_n, GB_DECIDE);
    else if (op == 3) { dmu.back.pointing = 1; btn_try_pointing(&dmu.back, 1); } /* onPointing */
}
int menu_dome_back_state(int q)
{
    if (!dmu.back_l) return q >= 2;
    switch (q) {
    case 0: return dmu.back_alive && pointing_on(dmu.back_l, "BoxButton"); /* isPointing */
    case 1: return dmu.back_alive && btn_appearing(&dmu.back);
    case 2: return !dmu.back_alive || btn_is(&dmu.back, BP_DISAPPEAR) || btn_is(&dmu.back, BP_TODISAPPEAR) || btn_hidden(&dmu.back);
    default: return !dmu.back_alive;
    }
}
static void dmu_back_movement(void)
{
    if (!dmu.back_alive) return;
    switch (nrv_begin(&dmu.back_n)) {
    case GB_APPEAR: if (FIRST(&dmu.back_n)) btn_appear(&dmu.back); if (!btn_appearing(&dmu.back)) nrv_set(&dmu.back_n, GB_WAIT); break;
    case GB_WAIT: break;
    case GB_DISAPPEAR:
        if (FIRST(&dmu.back_n)) btn_disappear(&dmu.back);
        if (btn_hidden(&dmu.back) && dmu.back_n.step > 0) dmu.back_alive = 0;
        break;
    case GB_DECIDE:
        if (FIRST(&dmu.back_n)) { dmu.back.selected = 1; nrv_set(&dmu.back.n, BP_DECIDED); }
        if (btn_hidden(&dmu.back) && dmu.back_n.step > 0) dmu.back_alive = 0;
        break;
    }
    nrv_end(&dmu.back_n);
    btn_update(&dmu.back);
    lyt_update(dmu.back_l);
}

/* GalaxyConfirmLayout ("GalaxySelect": its StartButton and a BackButton) */
void menu_dome_confirm_appear(void)
{
    if (!dmu.conf) return;
    btn_appear(&dmu.start);
    btn_appear(&dmu.conf_back);
    dmu.conf_back_clicked = 0;
    dmu.conf_alive = 1;
    dmu.conf_n.next = -1;
    nrv_set(&dmu.conf_n, GC_SELECTING);
}
int menu_dome_confirm(void) /* isSelected: 1 Yes (isSelectedYes), 2 No; 0 still choosing */
{
    if (!dmu.conf) return 1;
    if (!nrv_is(&dmu.conf_n, GC_SELECTED) && !nrv_is(&dmu.conf_n, GC_SELECTEDBACK)) return 0;
    return dmu.start.selected ? 1 : 2;
}
static void dmu_conf_movement(void)
{
    if (!dmu.conf_alive) return;
    switch (nrv_begin(&dmu.conf_n)) {
    case GC_SELECTING:
        if (dmu.conf_back_clicked) { nrv_set(&dmu.conf_n, GC_SELECTEDBACK); break; }
        if (btn_pointing_trigger(&dmu.start)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
        if (btn_try_select(&dmu.start)) {
            se_start(SE_SY_GALAXY_DECIDE_OK, NULL);
            btn_disappear(&dmu.conf_back);
            nrv_set(&dmu.conf_n, GC_SELECTED);
        } else if (!btn_appearing(&dmu.start) && !btn_appearing(&dmu.conf_back) && (pad_pressed(PAD_CIRCLE) || pad_pressed(PAD_TRIANGLE))) {
            se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); /* B */
            btn_disappear(&dmu.start);
            btn_disappear(&dmu.conf_back);
            nrv_set(&dmu.conf_n, GC_SELECTED);
        }
        break;
    case GC_SELECTEDBACK:
        if (btn_is(&dmu.conf_back, BP_DISAPPEAR) || btn_hidden(&dmu.conf_back)) { btn_disappear(&dmu.start); nrv_set(&dmu.conf_n, GC_SELECTED); }
        break;
    case GC_SELECTED:
        if (btn_hidden(&dmu.start) && btn_hidden(&dmu.conf_back) && dmu.conf_n.step > 0) dmu.conf_alive = 0;
        break;
    }
    nrv_end(&dmu.conf_n);
    btn_update(&dmu.start);
    lyt_update(dmu.conf);
    /* its BackButton::control */
    if (btn_pointing_trigger(&dmu.conf_back)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
    if (btn_try_select(&dmu.conf_back)) {
        if (!dmu.conf_back_clicked) se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL);
        dmu.conf_back_clicked = 1;
    }
    btn_update(&dmu.conf_back);
    lyt_update(dmu.conf_back_l);
}

/* GalaxyNamePlate::show / showUnknown / showNew / showOpen (type 0 / 1 / 2) and setPos3D (x, y on the screen) */
void menu_dome_plate(int i, const char *g, int type, int immediate, float x, float y)
{
    if (i < 0 || i >= DOME_PLATES || !dmu.plate[i].l) return;
    DomePlate *p = &dmu.plate[i];
    char label[64];
    if (!p->alive) {
        p->alive = 1; p->n.next = -1;
        nrv_set(&p->n, immediate ? NP_APPEAR : NP_APPEARREADY);
    } else if (immediate) {
        if (nrv_is(&p->n, NP_APPEARREADY)) nrv_set(&p->n, NP_APPEAR);
    } else if (p->last_immediate) nrv_set(&p->n, NP_APPEARREADY);
    lyt_show_rec(p->l, "GalaxyNamePlate", 1);
    lyt_show_rec(p->l, "GalaxyNamePlateU", 0);
    if (type == 2) snprintf(label, sizeof label, "GalaxyNameShort_%s", g);
    lyt_set_text_rec(p->l, "GalaxyName", msg_get(type == 0 ? "GalaxyNameShort_Unknown" : type == 1 ? "GalaxyNameShort_New" : label));
    lyt_set_frame(p->l, 2, lyt_text_width(p->l, "TxtGalaxyName")); /* setAnimFrameAndStopAdjustTextWidth (OneLine) */
    lyt_set_rate(p->l, 2, 0);
    lyt_start(p->l, "Unknown", 1);
    lyt_set_frame(p->l, 1, (float)type);
    lyt_set_rate(p->l, 1, 0);
    lyt_set_pos(p->l, (x - 320.0f) * (608.0f / 640.0f), (224.0f - y) * (456.0f / 448.0f));
    p->last_immediate = (u8)immediate; p->shown = 1; p->type = (u8)type;
}

static void dmu_plate_movement(int i)
{
    DomePlate *p = &dmu.plate[i];
    if (!p->alive) return;
    switch (nrv_begin(&p->n)) {
    case NP_APPEARREADY:
        if (FIRST(&p->n)) p->layout_shown = 0;
        if (!p->shown) p->alive = 0;
        else if (p->n.step == 40) nrv_set(&p->n, NP_APPEAR); /* cAppearReadyFrame */
        break;
    case NP_APPEAR:
        if (FIRST(&p->n)) { lyt_start(p->l, "Appear", 0); p->layout_shown = 1; }
        if (!p->shown) p->alive = 0;
        else if (lyt_stopped(p->l, 0)) nrv_set(&p->n, NP_WAIT);
        break;
    case NP_WAIT:
        if (FIRST(&p->n)) lyt_start(p->l, "Wait", 0);
        if (!p->shown) p->alive = 0;
        break;
    }
    nrv_end(&p->n);
    p->shown = 0; /* control */
    lyt_update(p->l);
}

void menu_dome_open(void)
{
    dmu.on = 1;
    dmu.info_alive = dmu.back_alive = dmu.conf_alive = 0;
    dmu.info_frame = 0;
    for (int i = 0; i < DOME_PLATES; i++) dmu.plate[i].alive = 0;
}
void menu_dome_close(void) { dmu.on = 0; }

static void dmu_init(void)
{
    memset(&dmu, 0, sizeof dmu);
    dmu.info = lyt_new("GalaxyInfo");
    dmu.back_l = lyt_new("BackButton");
    dmu.conf = lyt_new("GalaxySelect");
    dmu.conf_back_l = lyt_new("BackButton");
    if (dmu.back_l) { btn_init(&dmu.back, dmu.back_l, "Back", "BoxButton", 0, 1); dmu.back.pointing_kept = 1; }
    if (dmu.conf && dmu.conf_back_l) {
        btn_init(&dmu.start, dmu.conf, "StartButton", "BoxStartButton", 0, 1);
        btn_init(&dmu.conf_back, dmu.conf_back_l, "Back", "BoxButton", 0, 1);
        lyt_show_rec(dmu.conf, "Counter", 0); /* ponytail: the file select's star counter, the dome has its own */
    } else dmu.conf = NULL;
    if (!dmu.info) return;
    for (int i = 0; i < DOME_PLATES; i++)
        if ((dmu.plate[i].l = lyt_new("GalaxyNamePlate"))) lyt_start(dmu.plate[i].l, "OneLine", 2);
}

static void dmu_update(void)
{
    if (!dmu.on) return;
    dmu_info_movement();
    dmu_back_movement();
    dmu_conf_movement();
    for (int i = 0; i < DOME_PLATES; i++) dmu_plate_movement(i);
}

static void dmu_draw(void)
{
    if (!dmu.on) return;
    for (int i = 0; i < DOME_PLATES; i++) if (dmu.plate[i].alive && dmu.plate[i].layout_shown) lyt_draw(dmu.plate[i].l);
    if (dmu.info_alive) lyt_draw(dmu.info);
    if (dmu.back_alive) lyt_draw(dmu.back_l);
    if (dmu.conf_alive) { lyt_draw(dmu.conf); lyt_draw(dmu.conf_back_l); }
}

int menu_test_point_dome(const char *pane) /* tests: the pointer on a pane of the dome's confirm / back button */
{
    float x, y;
    if (!lyt_pane_center(dmu.conf_alive ? dmu.conf : dmu.back_l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}

/* --- FileSelector (the FileSelect stage): TitleSequenceProduct, FileSelectItem, FileSelectCameraController,
 * FileSelectSky, FileSelectInfo, FileSelectNumber, FileSelectButton, BackButton after Petari, as a whole-screen menu
 * (its stage's world not drawn: FileSelector draws everything it shows). A+B is X+Circle held; the pointer is the
 * right stick, X decides, Circle / Triangle is B. ponytail: no Mii / icon select (a new file is Mario's), no
 * Mii / Manual buttons, no copy source marker (FileSelectEffect), no BrosButton (no Luigi), no blink / morph blur / effects / ME / BGM, no swipe
 * spin, the save's date not shown */
enum { FS_OFF, FS_TITLE, FS_TITLEEND, FS_SELECTSTART, FS_SELECT, FS_CONFIRMSTART, FS_CONFIRM, FS_DEMOSTARTWAIT,
       FS_CREATECONFIRMSTART, FS_CREATECONFIRM, FS_CREATE, FS_DELETECONFIRMSTART, FS_DELETECONFIRM, FS_DELETE,
       FS_COPYWAIT, FS_COPYSELECT, FS_COPYCONFIRMSTART, FS_COPYCONFIRM, FS_COPYDEMO };
enum { TS_LOGOFADEIN, TS_LOGOWAIT, TS_LOGODISPLAY, TS_DECIDE, TS_DEAD };
enum { FC_TITLE, FC_MOVEFAR, FC_FAR, FC_MOVENEAR, FC_NEAR };
enum { FI_NEW, FI_EXIST, FI_FORMAT, FI_CHANGE };
enum { FB_START, FB_DELETE, FB_COPY, FB_N };
static const int fs_order[6] = {1, 2, 4, 6, 5, 3}; /* sIndexOrder: slot i shows file fs_order[i] */
static const float fs_theta_off[6] = {10, -10, 0, 0, 0, 0}; /* sItemThetaOffset */
static const char *const fs_fellow[5] = {"FileSelectDataMario", "FileSelectDataLuigi", "FileSelectDataYoshi",
                                         "FileSelectDataKinopio", "FileSelectDataPeach"};

typedef struct {
    int file;                           /* 1..6 */
    float pos[3], target[3], rot_y, speed, scale, scale_from;
    int scale_goal, scale_step;         /* ScaleController: 1 ToBig, 0 ToSmall, -1 idle */
    int selectable, pointed, rotate_on, front_step, front_n;
    int model, icon;                    /* FI_*: what it shows; its fellow (icon 1..5) */
    Nrv n;
    LytInst *num;                       /* FileSelectNumber */
    int num_on, num_end;
} FsItem;

static struct {
    Nrv n, title, cam;
    LytInst *logo, *press, *info, *op, *back_l;
    int logo_on, press_on, a_prev, b_prev;
    FsItem item[6];
    int sel, hover, pointed;            /* _B4 the chosen slot, _BC hovered, _C0 this frame's (-1 none) */
    int src;                            /* _B8 the copy's source slot */
    u16 wtext[200], num[2][4];
    float tgt[3], eye[3], fovy, ptgt[3], pfovy, near_tgt[3];
    int sky_step; float sky_x, sky_y;
    const Model *m_planet, *m_fellow[5], *m_sky;
    Btn btn[FB_N], back; u8 back_clicked, op_on, back_on;
    int info_on, info_dir;
    SysInfo win;
} fsl;

static void fs_v3(float *d, float x, float y, float z) { d[0] = x; d[1] = y; d[2] = z; }

static void fs_base_pos(float off) /* calcBasePos: the ring, tilted 20 degrees about X, the chosen one left out */
{
    float c = cosf(20.0f * 0.017453293f), s = sinf(20.0f * 0.017453293f);
    for (int i = 0; i < 6; i++) {
        if (i == fsl.sel) continue;
        float th = -(i + 4) * (6.2831853f / 6) - fs_theta_off[i] * 0.017453293f, x = 5000.0f * cosf(th), z = 5000.0f * sinf(th);
        fs_v3(fsl.item[i].target, x, -s * z + off, c * z);
    }
}

static void fs_to_screen(const float p[3], float *x, float *y, float *z) /* calcScreenPosition with this camera */
{
    float f[3] = {fsl.ptgt[0] - fsl.eye[0], fsl.ptgt[1] - fsl.eye[1], fsl.ptgt[2] - fsl.eye[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (int k = 0; k < 3; k++) f[k] /= fl;
    float r[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0}; /* f x up(0, 1, 0) */
    float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (int k = 0; k < 3; k++) r[k] /= rl;
    float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    float d[3] = {p[0] - fsl.eye[0], p[1] - fsl.eye[1], p[2] - fsl.eye[2]};
    float zz = d[0] * f[0] + d[1] * f[1] + d[2] * f[2], k = 224.0f / tanf(fsl.pfovy * 3.14159265f / 360.0f);
    *z = zz;
    if (zz < 1) zz = 1;
    *x = 320.0f + (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) * k / zz;
    *y = 224.0f - (d[0] * u[0] + d[1] * u[1] + d[2] * u[2]) * k / zz;
}

/* --- FileSelectCameraController --- */
static void fs_cam_move(const float tgt[3], const float eye[3], float fovy)
{
    float t = fsl.cam.step / 60.0f;
    t = t * t > 1 ? 1 : t * t;
    for (int k = 0; k < 3; k++) { fsl.tgt[k] += (tgt[k] - fsl.tgt[k]) * t; fsl.eye[k] += (eye[k] - fsl.eye[k]) * t; }
    fsl.fovy += (fovy - fsl.fovy) * t;
}
static void fs_cam_update(void)
{
    static const float far_t[3] = {0, 800, 0}, far_e[3] = {0, 0, 15000};
    float ne[3] = {fsl.near_tgt[0], fsl.near_tgt[1], fsl.near_tgt[2] + 4800.0f};
    switch (nrv_begin(&fsl.cam)) {
    case FC_TITLE: fs_v3(fsl.tgt, 0, 15800, 0); fs_v3(fsl.eye, 0, 15800, 15000); fsl.fovy = 60; break;
    case FC_MOVEFAR: fs_cam_move(far_t, far_e, 40); if (fsl.cam.step == 60) nrv_set(&fsl.cam, FC_FAR); break;
    case FC_FAR: memcpy(fsl.tgt, far_t, 12); memcpy(fsl.eye, far_e, 12); fsl.fovy = 40; break;
    case FC_MOVENEAR: fs_cam_move(fsl.near_tgt, ne, 50); if (fsl.cam.step == 60) nrv_set(&fsl.cam, FC_NEAR); break;
    case FC_NEAR: fsl.fovy = 50; memcpy(fsl.eye, ne, 12); break;
    }
    nrv_end(&fsl.cam);
}
static void fs_far(void) { fsl.cam.next = -1; nrv_set(&fsl.cam, FC_MOVEFAR); }
static void fs_near(void) /* goToNearPoint: the rest drop 16000, the camera to the chosen one */
{
    fs_base_pos(-16000.0f);
    const float *p = fsl.item[fsl.sel].target;
    fs_v3(fsl.near_tgt, p[0], p[1] + 1100.0f, p[2]);
    nrv_set(&fsl.cam, FC_MOVENEAR);
}
static int fs_cam_at(int nrv) { return nrv_is(&fsl.cam, nrv); }

/* --- FileSelectItem --- */
static void fs_item_scale(FsItem *it, int big) { it->scale_goal = big; it->scale_step = 0; it->scale_from = it->scale; }
static void fs_item_offpoint(FsItem *it)
{
    if (!it->pointed) return;
    it->pointed = 0;
    if (it->num) lyt_start(it->num, "SelectOut", 1);
    fs_item_scale(it, 0);
}
static void fs_item_onpoint(FsItem *it) /* onPointing */
{
    if (!it->selectable || it->pointed) return;
    it->pointed = 1;
    if (it->num) lyt_start(it->num, "SelectIn", 1);
    fs_item_scale(it, 1);
}
static void fs_item_invalidate(FsItem *it) { fs_item_offpoint(it); it->selectable = 0; }
static void fs_items_select(int on) { for (int i = 0; i < 6; i++) { if (on) fsl.item[i].selectable = 1; else fs_item_invalidate(&fsl.item[i]); } }
static void fs_numbers(int on)
{
    for (int i = 0; i < 6; i++) {
        FsItem *it = &fsl.item[i];
        if (!it->num) continue;
        if (on) { it->num_on = 1; it->num_end = 0; lyt_start(it->num, "Appear", 0); lyt_start(it->num, "SelectOut", 1); lyt_set_frame(it->num, 1, lyt_anim_frames(it->num, "SelectOut") - 1); }
        else if (it->num_on) { it->num_end = 1; lyt_start(it->num, "End", 0); }
    }
}
static void fs_item_update(FsItem *it, int slot)
{
    switch (nrv_begin(&it->n)) {
    case FI_FORMAT: /* format: the fellow vanishes at 40, the planet back */
        if (it->n.step == 40) { se_start(SE_SY_FILE_SEL_MORPH_DUMMY, NULL); it->model = FI_NEW; }
        if (it->n.step >= 40) { it->rot_y = 0; it->speed = 0; }
        if (it->n.step == 60) nrv_set(&it->n, FI_NEW);
        break;
    case FI_CHANGE: /* change: the fellow out of the planet at 40 */
        if (it->n.step == 40) { se_start(SE_SY_FILE_SEL_MORPH_MARIO, NULL); it->model = FI_EXIST; }
        if (it->n.step >= 40) { it->rot_y = 0; it->speed = 0; }
        if (it->n.step == 150) nrv_set(&it->n, FI_EXIST);
        break;
    }
    nrv_end(&it->n);
    /* control: updatePointing, updateRotate, the scale controller, its number's place */
    for (int k = 0; k < 3; k++) it->pos[k] = it->target[k] * 0.05f + it->pos[k] * 0.95f;
    if (it->front_n) { /* turnToFront */
        float r = (float)++it->front_step / it->front_n, a = fmodf(it->rot_y + 180.0f, 360.0f);
        if (a < 0) a += 360.0f;
        a -= 180.0f;
        r = r * r > 1 ? 1 : r * r;
        it->rot_y = a - a * r; it->speed = 0;
        if (it->front_step >= it->front_n) it->front_n = 0;
    } else if (it->rotate_on) {
        it->speed *= 0.98f;
        if (it->speed >= 0 && it->speed < 0.5f) it->speed = 0.5f;
        else if (it->speed < 0 && it->speed > -0.5f) it->speed = -0.5f;
        it->rot_y = fmodf(it->rot_y + it->speed + 360.0f, 360.0f);
    }
    if (it->scale_goal >= 0) {
        float goal = it->scale_goal ? 1.2f : 1.0f;
        it->scale_step++;
        it->scale += (it->scale_step / 30.0f) * (goal - it->scale);
        if (it->scale_step >= 30) { it->scale = goal; it->scale_goal = -1; }
    }
    if (it->num) {
        float p[3] = {it->pos[0], it->pos[1] + 2150.0f, it->pos[2]}, x, y, z;
        fs_to_screen(p, &x, &y, &z);
        lyt_set_pos(it->num, (x - 320.0f) * (608.0f / 640.0f), (224.0f - y) * (456.0f / 448.0f));
        lyt_set_number_rec(it->num, "FileNumber", it->file);
        if (it->num_on) {
            if (lyt_stopped(it->num, 0) && !it->num_end) lyt_start(it->num, "Wait", 0);
            if (it->num_end && lyt_stopped(it->num, 0)) it->num_on = 0;
            lyt_update(it->num);
        }
    }
    (void)slot;
}
static int fs_item_pointing(const FsItem *it) /* the star pointer within 1000 x scale round (0, 900, 0) up */
{
    float cx, cy, x, y, z;
    if (!game_cursor(&cx, &cy)) return 0;
    float p[3] = {it->pos[0], it->pos[1] + 900.0f * it->scale, it->pos[2]};
    fs_to_screen(p, &x, &y, &z);
    if (z <= 0) return 0;
    float r = 1000.0f * it->scale * (224.0f / tanf(fsl.pfovy * 3.14159265f / 360.0f)) / z;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r;
}

/* --- FileSelectInfo --- */
static void fs_info_set(int slot) /* setFileInfo */
{
    FsItem *it = &fsl.item[slot];
    int stars, pieces, ne, ce;
    gd_file_info(it->file - 1, &stars, &pieces, &ne, &ce);
    char label[40];
    snprintf(label, sizeof label, "System_FileSelect_Icon%03d", gd_file_icon(it->file - 1) > 0 ? gd_file_icon(it->file - 1) - 1 : 0);
    lyt_set_number_rec(fsl.info, "FileNumber", it->file);
    lyt_set_text_rec(fsl.info, "FileName", msg_get(label));
    lyt_set_number_rec(fsl.info, "Star", stars);
    lyt_set_number_rec(fsl.info, "StarPiece", pieces);
    lyt_set_text_rec(fsl.info, "TxtDay", NULL);
    lyt_set_text_rec(fsl.info, "TxtTime", NULL);
    lyt_show_rec(fsl.info, "MissCounter", 0);
    lyt_show_rec(fsl.info, "Complete1", ne);
    lyt_show_rec(fsl.info, "Complete2", ce);
    lyt_show_rec(fsl.info, "BrosIcon", 0);
}
static void fs_info_appear(void)
{
    if (!fsl.info) return;
    if (!fsl.info_on) { lyt_start(fsl.info, "Appear", 0); lyt_start(fsl.info, "MarioWait", 2); lyt_start(fsl.info, "ButtonAppear", 1); lyt_set_frame(fsl.info, 1, 0); lyt_set_rate(fsl.info, 1, 0); }
    else lyt_set_rate(fsl.info, 0, 1.0f);
    fsl.info_on = 1; fsl.info_dir = 1;
}
static void fs_info_disappear(void) /* the Appear animation backward at a quarter (sDisappearAnimRate) */
{
    if (!fsl.info_on) return;
    fsl.info_dir = -1;
    lyt_set_frame(fsl.info, 0, lyt_frame(fsl.info, 0));
    lyt_set_rate(fsl.info, 0, -0.25f);
}
static void fs_info_update(void)
{
    if (!fsl.info_on) return;
    lyt_update(fsl.info);
    if (fsl.info_dir < 0 && lyt_frame(fsl.info, 0) <= 0.25f) fsl.info_on = 0;
}

static void fs_update_file_info(void) /* updateFileInfo: the hovered file's info */
{
    if (fsl.pointed >= 0 && fsl.pointed != fsl.hover) {
        if (fsl.hover >= 0) { fs_item_offpoint(&fsl.item[fsl.hover]); fs_info_disappear(); }
        if (gd_file_created(fsl.item[fsl.pointed].file - 1)) { fs_info_set(fsl.pointed); fs_info_appear(); }
        fsl.hover = fsl.pointed;
        fs_item_onpoint(&fsl.item[fsl.hover]);
    } else if (fsl.pointed < 0 && fsl.hover >= 0) {
        fs_item_offpoint(&fsl.item[fsl.hover]); fs_info_disappear(); fsl.hover = -1;
    }
}
static void fs_clear_pointing(void) { if (fsl.hover >= 0) fs_item_offpoint(&fsl.item[fsl.hover]); fsl.hover = -1; }

static void fs_layouts_out(void) /* disappearAllLayout */
{
    if (fsl.op_on) { for (int b = 0; b < FB_N; b++) btn_disappear(&fsl.btn[b]); lyt_start(fsl.info, "ButtonEnd", 1); }
    fs_info_disappear();
    if (fsl.back_on && !fsl.back_clicked) btn_disappear(&fsl.back);
}
static int fs_layouts_hidden(void)
{
    for (int b = 0; b < FB_N; b++) if (fsl.op_on && !btn_hidden(&fsl.btn[b])) return 0;
    return !fsl.info_on && (!fsl.back_on || btn_hidden(&fsl.back));
}
static int fs_back_selected(void) /* checkSelectedBackButton */
{
    if (!fsl.back_on || btn_hidden(&fsl.back)) return 0;
    if (fsl.back_clicked) return 1;
    if (pad_pressed(PAD_CIRCLE) || pad_pressed(PAD_TRIANGLE)) { se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); btn_disappear(&fsl.back); return 1; }
    return 0;
}

static void fs_title_update(void) /* TitleSequenceProduct */
{
    int a = pad_held(PAD_CROSS), b = pad_held(PAD_CIRCLE);
    switch (nrv_begin(&fsl.title)) {
    case TS_LOGOFADEIN:
        if (FIRST(&fsl.title)) { fsl.logo_on = 1; lyt_start(fsl.logo, "Appear", 0); }
        if (lyt_stopped(fsl.logo, 0)) nrv_set(&fsl.title, TS_LOGOWAIT);
        break;
    case TS_LOGOWAIT:
        if (FIRST(&fsl.title)) lyt_start(fsl.logo, "Wait", 0);
        if (fsl.title.step == 25) nrv_set(&fsl.title, TS_LOGODISPLAY); /* sPressABAppearFrame */
        break;
    case TS_LOGODISPLAY:
        if (FIRST(&fsl.title)) { fsl.press_on = 1; lyt_start(fsl.press, "Appear", 0); }
        if (lyt_stopped(fsl.press, 0)) lyt_start(fsl.press, "Wait", 0);
        if (a && b) {
            se_start(SE_SY_GAME_START, NULL);
            nrv_set(&fsl.title, TS_DECIDE);
            break;
        }
        if (a && !fsl.a_prev) { lyt_start(fsl.logo, "ReactionA", 1); lyt_set_frame(fsl.logo, 1, 0); lyt_set_rate(fsl.logo, 1, 0); }
        else if (!a && fsl.a_prev) lyt_start(fsl.logo, "ReactionA", 1);
        if (b && !fsl.b_prev) { lyt_start(fsl.logo, "ReactionB", 1); lyt_set_frame(fsl.logo, 1, 0); lyt_set_rate(fsl.logo, 1, 0); }
        else if (!b && fsl.b_prev) lyt_start(fsl.logo, "ReactionB", 1);
        if ((a && !fsl.a_prev) || (b && !fsl.b_prev)) lyt_start(fsl.press, "ButtonReaction", 0);
        break;
    case TS_DECIDE:
        if (FIRST(&fsl.title)) { lyt_start(fsl.logo, "Decide", 0); lyt_start(fsl.press, "End", 0); }
        if (lyt_stopped(fsl.logo, 0) && lyt_stopped(fsl.press, 0)) nrv_set(&fsl.title, TS_DEAD);
        break;
    case TS_DEAD:
        if (FIRST(&fsl.title)) fsl.logo_on = fsl.press_on = 0;
        break;
    }
    nrv_end(&fsl.title);
    fsl.a_prev = a; fsl.b_prev = b;
    if (fsl.logo_on) lyt_update(fsl.logo);
    if (fsl.press_on) lyt_update(fsl.press);
}

static void fs_update(void)
{
    /* the hovered item: the pointed selectable one with the smallest file number (notifyItem 0) */
    fsl.pointed = -1;
    for (int i = 0; i < 6; i++)
        if (fsl.item[i].selectable && fs_item_pointing(&fsl.item[i]) && !(fsl.back_on && pointing_on(fsl.back_l, "BoxButton")) &&
            (fsl.pointed < 0 || fsl.item[i].file < fsl.item[fsl.pointed].file))
            fsl.pointed = i;
    switch (nrv_begin(&fsl.n)) {
    case FS_TITLE:
        fs_title_update();
        if (nrv_is(&fsl.title, TS_DEAD)) nrv_set(&fsl.n, FS_TITLEEND);
        break;
    case FS_TITLEEND:
        if (FIRST(&fsl.n)) { fs_far(); fs_base_pos(0); fs_items_select(0); }
        if (fs_cam_at(FC_FAR)) { for (int i = 0; i < 6; i++) fsl.item[i].rotate_on = 1; nrv_set(&fsl.n, FS_SELECT); }
        break;
    case FS_SELECTSTART:
        if (FIRST(&fsl.n)) { fsl.sel = -1; fs_far(); fs_base_pos(0); }
        if (fs_cam_at(FC_FAR)) nrv_set(&fsl.n, FS_SELECT);
        break;
    case FS_SELECT:
        if (FIRST(&fsl.n)) { fs_items_select(1); fs_numbers(1); }
        else fs_update_file_info();
        if (fsl.hover >= 0 && fsl.item[fsl.hover].pointed && pad_pressed(PAD_CROSS)) { /* onSelect */
            fsl.sel = fsl.hover;
            se_start(SE_SY_GALAXY_SELECTED, NULL);
            fs_item_scale(&fsl.item[fsl.sel], 0);
            nrv_set(&fsl.n, gd_file_created(fsl.item[fsl.sel].file - 1) ? FS_CONFIRMSTART : FS_CREATECONFIRMSTART);
        }
        break;
    case FS_CONFIRMSTART:
        if (FIRST(&fsl.n)) { fs_near(); fs_items_select(0); fs_numbers(0); fsl.item[fsl.sel].front_n = 40; fsl.item[fsl.sel].front_step = 0; }
        if (fs_cam_at(FC_NEAR)) nrv_set(&fsl.n, FS_CONFIRM);
        break;
    case FS_CONFIRM:
        if (FIRST(&fsl.n)) {
            btn_appear(&fsl.btn[FB_START]); btn_appear(&fsl.btn[FB_DELETE]); btn_appear(&fsl.btn[FB_COPY]); fsl.op_on = 1;
            btn_appear(&fsl.back); fsl.back_on = 1; fsl.back_clicked = 0;
            if (!fsl.info_on) { fs_info_set(fsl.sel); fs_info_appear(); }
            lyt_start(fsl.info, "ButtonAppear", 1); /* slide */
        }
        if (fs_back_selected()) { fs_layouts_out(); fs_clear_pointing(); nrv_set(&fsl.n, FS_SELECTSTART); break; }
        for (int b = 0; b < FB_N; b++) if (btn_pointing_trigger(&fsl.btn[b])) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
        if (btn_try_select(&fsl.btn[FB_START])) nrv_set(&fsl.n, FS_DEMOSTARTWAIT);
        else if (btn_try_select(&fsl.btn[FB_DELETE])) nrv_set(&fsl.n, FS_DELETECONFIRMSTART);
        else if (btn_try_select(&fsl.btn[FB_COPY])) nrv_set(&fsl.n, FS_COPYWAIT);
        break;
    case FS_DEMOSTARTWAIT:
        if (FIRST(&fsl.n)) { se_start(SE_SY_FILE_SELECTED, NULL); fs_layouts_out(); wipe_start(60, 1, 0); }
        if (!wipe_active() && fsl.n.step > 60) { /* Demo: startGameDataLoadSequence, requestChangeStageInGameAfterLoadingGameData */
            gd_file_load(fsl.item[fsl.sel].file - 1);
            seq_set_open_wipe(30, 0);
            seq_galaxy_move(SEQ_AFTER_LOADING, NULL, 1, -1, 0);
            nrv_set(&fsl.n, FS_OFF);
        }
        break;
    case FS_CREATECONFIRMSTART:
        if (FIRST(&fsl.n)) { fs_near(); fs_items_select(0); fs_numbers(0); }
        if (fs_cam_at(FC_NEAR)) nrv_set(&fsl.n, FS_CREATECONFIRM);
        break;
    case FS_CREATECONFIRM:
        if (FIRST(&fsl.n)) si_appear(&fsl.win, "System_FileSelect001", SI_YESNO);
        if (!fsl.win.alive && fsl.n.step > 0) {
            if (yn_yes(&fsl.win.yn)) { se_start(SE_SY_FILE_SEL_NEW_FILE, NULL); nrv_set(&fsl.n, FS_CREATE); }
            else { fs_clear_pointing(); nrv_set(&fsl.n, FS_SELECTSTART); }
        }
        break;
    case FS_CREATE: /* Create, the icon (Mario), MiiCreateDemo's change */
        if (FIRST(&fsl.n)) {
            FsItem *it = &fsl.item[fsl.sel];
            gd_file_create(it->file - 1, 1);
            menu_save_request();
            it->icon = 1; it->n.next = -1; nrv_set(&it->n, FI_CHANGE);
        }
        if (nrv_is(&fsl.item[fsl.sel].n, FI_EXIST)) { fs_info_set(fsl.sel); nrv_set(&fsl.n, FS_CONFIRM); }
        break;
    case FS_DELETECONFIRMSTART:
        if (FIRST(&fsl.n)) { se_start(SE_SY_FILE_SEL_UPPER_DECIDE, NULL); fs_layouts_out(); }
        if (fs_layouts_hidden()) nrv_set(&fsl.n, FS_DELETECONFIRM);
        break;
    case FS_DELETECONFIRM:
        if (FIRST(&fsl.n)) si_appear(&fsl.win, "System_FileSelect007", SI_YESNO);
        if (!fsl.win.alive && fsl.n.step > 0) {
            if (yn_yes(&fsl.win.yn)) { se_start(SE_SY_FILE_SEL_DELETE, NULL); nrv_set(&fsl.n, FS_DELETE); }
            else nrv_set(&fsl.n, FS_CONFIRM);
        }
        break;
    case FS_DELETE:
        if (FIRST(&fsl.n)) {
            FsItem *it = &fsl.item[fsl.sel];
            gd_file_delete(it->file - 1);
            menu_save_request();
            it->n.next = -1; nrv_set(&it->n, FI_FORMAT);
        }
        if (nrv_is(&fsl.item[fsl.sel].n, FI_NEW) && fsl.n.step > 0) { fs_clear_pointing(); nrv_set(&fsl.n, FS_SELECTSTART); }
        break;
    case FS_COPYWAIT: /* the source kept, the ring again */
        if (FIRST(&fsl.n)) {
            se_start(SE_SY_FILE_SEL_UPPER_DECIDE, NULL);
            fsl.src = fsl.sel; fs_layouts_out(); fs_clear_pointing();
            fsl.sel = -1; fs_far(); fs_base_pos(0);
        }
        if (fs_cam_at(FC_FAR)) nrv_set(&fsl.n, FS_COPYSELECT);
        break;
    case FS_COPYSELECT:
        if (FIRST(&fsl.n)) {
            fs_items_select(1); fs_item_invalidate(&fsl.item[fsl.src]);
            btn_appear(&fsl.back); fsl.back_on = 1; fsl.back_clicked = 0;
            fs_numbers(1);
        }
        fs_update_file_info();
        if (fs_back_selected()) { /* back: zoom on the source again */
            fs_items_select(0); fs_info_disappear();
            fsl.sel = fsl.src; nrv_set(&fsl.n, FS_CONFIRMSTART);
            break;
        }
        if (fsl.hover >= 0 && fsl.item[fsl.hover].pointed && pad_pressed(PAD_CROSS)) {
            fsl.sel = fsl.hover;
            se_start(SE_SY_GALAXY_SELECTED, NULL);
            nrv_set(&fsl.n, FS_COPYCONFIRMSTART);
        }
        break;
    case FS_COPYCONFIRMSTART:
        if (FIRST(&fsl.n)) { fs_layouts_out(); fs_items_select(0); }
        if (fs_layouts_hidden()) nrv_set(&fsl.n, FS_COPYCONFIRM);
        break;
    case FS_COPYCONFIRM:
        if (FIRST(&fsl.n)) { /* "copy file <src> to <dst>?" (016 to a new one, 014 over one) */
            const char *label = gd_file_created(fsl.item[fsl.sel].file - 1) ? "System_FileSelect014" : "System_FileSelect016";
            const u16 *args[2] = {fsl.num[0], fsl.num[1]};
            fs_clear_pointing();
            fsl.num[0][0] = (u16)('0' + fsl.item[fsl.src].file); fsl.num[0][1] = 0;
            fsl.num[1][0] = (u16)('0' + fsl.item[fsl.sel].file); fsl.num[1][1] = 0;
            si_appear(&fsl.win, label, SI_YESNO);
            msg_with_args(fsl.wtext, 200, msg_get(label), args, 2);
            lyt_set_text_rec(fsl.win.l, "InfoWindow", fsl.wtext);
        }
        if (!fsl.win.alive && fsl.n.step > 0) {
            if (yn_yes(&fsl.win.yn)) { /* CopySave: copyUserFileMemory + save all */
                gd_file_copy(fsl.item[fsl.sel].file - 1, fsl.item[fsl.src].file - 1);
                menu_save_request();
                nrv_set(&fsl.n, FS_COPYDEMO);
            } else nrv_set(&fsl.n, FS_COPYSELECT);
        }
        break;
    case FS_COPYDEMO:
        if (FIRST(&fsl.n)) {
            FsItem *it = &fsl.item[fsl.sel];
            se_start(SE_SY_FILE_SEL_COPY, NULL);
            it->icon = gd_file_icon(it->file - 1); it->n.next = -1; nrv_set(&it->n, FI_CHANGE);
            if (fsl.back_on) btn_disappear(&fsl.back);
        }
        if (nrv_is(&fsl.item[fsl.sel].n, FI_EXIST) && fsl.n.step > 0) { fs_clear_pointing(); fsl.sel = -1; nrv_set(&fsl.n, FS_SELECT); }
        break;
    }
    nrv_end(&fsl.n);
    /* control */
    for (int i = 0; i < 6; i++) fs_item_update(&fsl.item[i], i);
    fs_cam_update();
    for (int b = 0; b < FB_N; b++) btn_update(&fsl.btn[b]);
    if (fsl.op_on) { lyt_update(fsl.op); int all = 1; for (int b = 0; b < FB_N; b++) all &= btn_hidden(&fsl.btn[b]); if (all && !nrv_is(&fsl.n, FS_CONFIRM)) fsl.op_on = 0; }
    if (fsl.back_on) { /* BackButton::control */
        if (btn_pointing_trigger(&fsl.back)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
        if (btn_try_select(&fsl.back)) { if (!fsl.back_clicked) se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); fsl.back_clicked = 1; }
        btn_update(&fsl.back);
        lyt_update(fsl.back_l);
        if (btn_hidden(&fsl.back) && !btn_appearing(&fsl.back) && !nrv_is(&fsl.n, FS_CONFIRM)) fsl.back_on = 0;
    }
    fs_info_update();
    si_update(&fsl.win);
    /* FileSelectSky::exeWait */
    fsl.sky_step++;
    fsl.sky_x = 0.5f * (1.0f - cosf(fsl.sky_step * 3.14159265f / 3000.0f)) * 3.14159265f * 0.75f;
    fsl.sky_y += 0.001f;
}

static void fs_init(const Scene *s)
{
    memset(&fsl, 0, sizeof fsl);
    if (strncmp(game_stage_name, "FileSelect/", 11)) return;
    fsl.logo = lyt_new("TitleLogo"); fsl.press = lyt_new("PressStart"); fsl.info = lyt_new("FileInfo");
    fsl.op = lyt_new("FileSelect"); fsl.back_l = lyt_new("BackButton");
    fsl.m_planet = scene_model(s, "FileSelectDataPlanet");
    fsl.m_sky = scene_model(s, "CometNearOrbitSky");
    for (int i = 0; i < 5; i++) fsl.m_fellow[i] = scene_model(s, fs_fellow[i]);
    if (!fsl.logo || !fsl.press || !fsl.op || !fsl.back_l || !fsl.info) { printf("file select: layouts missing\n"); return; }
    lyt_hide_for_good(fsl.logo, "PicLogoGalaxy"); /* ponytail: its space through the letters' mask shows as a box */
    btn_init(&fsl.btn[FB_START], fsl.op, "StartButton", "BoxStartButton", 0, 1);
    btn_init(&fsl.btn[FB_DELETE], fsl.op, "DeleteButton", "PicDelete", 0, 1);
    btn_init(&fsl.btn[FB_COPY], fsl.op, "CopyButton", "PicCopy", 0, 1);
    lyt_show_pane(fsl.op, "MiiButton", 0); lyt_show_pane(fsl.op, "P2ManualButton", 0);
    btn_init(&fsl.back, fsl.back_l, "Back", "BoxButton", 0, 1);
    si_init(&fsl.win);
    for (int i = 0; i < 6; i++) {
        FsItem *it = &fsl.item[i];
        it->file = fs_order[i];
        it->scale = 1.0f; it->scale_goal = -1;
        it->model = gd_file_created(it->file - 1) ? FI_EXIST : FI_NEW;
        it->icon = gd_file_icon(it->file - 1);
        it->n.next = -1; nrv_set(&it->n, it->model);
        it->num = lyt_new("FileNumber");
    }
    fsl.sel = fsl.hover = -1;
    fs_base_pos(0);
    for (int i = 0; i < 6; i++) memcpy(fsl.item[i].pos, fsl.item[i].target, 12);
    fsl.n.next = -1; nrv_set(&fsl.n, FS_TITLE);
    fsl.title.next = -1; nrv_set(&fsl.title, TS_LOGOFADEIN);
    fsl.cam.next = -1; nrv_set(&fsl.cam, FC_TITLE);
    fs_v3(fsl.tgt, 0, 15800, 0); fs_v3(fsl.eye, 0, 15800, 15000); fsl.fovy = 60;
    memcpy(fsl.ptgt, fsl.tgt, 12); fsl.pfovy = 60;
    wipe_start(30, 0, 0); /* openWipeFade(30) */
    printf("file select: %d files\n", gd_file_created(0) + gd_file_created(1) + gd_file_created(2) + gd_file_created(3) + gd_file_created(4) + gd_file_created(5));
}
int menu_test_point_file(int slot, const char *pane) /* tests: the pointer on file slot `slot` (pane NULL) or a pane */
{
    float x, y, z;
    if (!pane) {
        const FsItem *it = &fsl.item[slot];
        float p[3] = {it->pos[0], it->pos[1] + 900.0f * it->scale, it->pos[2]};
        fs_to_screen(p, &x, &y, &z);
    } else if (!lyt_pane_center(fsl.win.alive ? fsl.win.l : fsl.op, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}
int menu_file_selecting(void) { return fsl.n.cur != FS_OFF || fsl.n.next > FS_OFF; }

static void fs_draw_model(const Model *md, const float pos[3], float rot_y, float scale)
{
    if (!md) return;
    float m[16], r[3] = {0, rot_y, 0}, k[3] = {scale, scale, scale};
    mat_translate_rot_scale(m, pos, r, k);
    render_model(md, m, 0x80, 0x80, 0x80);
}
static void fs_draw(void)
{
    float up[3] = {0, 1, 0};
    render_set_fovy(fsl.pfovy);
    render_set_shake(0, 0);
    render_begin(fsl.eye, fsl.ptgt, up);
    if (fsl.m_sky && fsl.m_sky->radius > 0) { /* FileSelectSky: inverse(RotY RotX), at the camera, fitted inside the far plane */
        float m[16], cy = cosf(-fsl.sky_y), sy = sinf(-fsl.sky_y), cx = cosf(-fsl.sky_x), sx = sinf(-fsl.sky_x), k = 108000.0f / fsl.m_sky->radius;
        mat_identity(m); /* (RotY RotX)^-1 = RotX(-x) RotY(-y) */
        m[0] = cy * k; m[1] = sx * sy * k; m[2] = -cx * sy * k;
        m[4] = 0; m[5] = cx * k; m[6] = sx * k;
        m[8] = sy * k; m[9] = -sx * cy * k; m[10] = cx * cy * k;
        m[12] = fsl.eye[0]; m[13] = fsl.eye[1]; m[14] = fsl.eye[2];
        render_model(fsl.m_sky, m, 0x80, 0x80, 0x80);
    }
    for (int i = 0; i < 6; i++) {
        const FsItem *it = &fsl.item[i];
        if (it->model == FI_NEW) { float p[3] = {it->pos[0], it->pos[1] + 900.0f * it->scale, it->pos[2]}; fs_draw_model(fsl.m_planet, p, it->rot_y, 30.0f * it->scale); }
        else fs_draw_model(fsl.m_fellow[it->icon >= 1 && it->icon <= 5 ? it->icon - 1 : 0], it->pos, it->rot_y, 30.0f * it->scale);
    }
    render_world_done();
    for (int i = 0; i < 6; i++) if (fsl.item[i].num_on) lyt_draw(fsl.item[i].num);
    if (fsl.info_on) lyt_draw(fsl.info);
    if (fsl.op_on) lyt_draw(fsl.op);
    if (fsl.back_on) lyt_draw(fsl.back_l);
    if (fsl.logo_on) lyt_draw(fsl.logo);
    if (fsl.press_on) lyt_draw(fsl.press);
    if (fsl.win.alive) lyt_draw(fsl.win.l);
    wipe_draw_quad();
    fade_draw();
    /* the camera's target and fovy a frame late (setProgrammableCameraParam with the previous ones) */
    memcpy(fsl.ptgt, fsl.tgt, 12); fsl.pfovy = fsl.fovy;
}

/* --- GalaxyMapController, GalaxyMap, its icons and PowerStarList after Petari (map.bin: the Observatory's), opened by
 * ButlerMap's talk (startGalaxyMapLayout / startAstroMapLayout); the world frozen while it shows. The pointer is the
 * right stick, X is A (an icon: its detail; empty space: the other view), Circle / Triangle is B. ponytail: the
 * normal modes only (no discover / Tico / dome lecture modes), no Tico / comet icons, no Mario icons, no AstroMapBoard
 * capture, the star list without its separators (belts), page turns or capture */
enum { GM_OFF, GM_READY, GM_FADEIN, GM_DISPLAY, GM_APPEARVIEW, GM_READYLIST, GM_APPEARLIST, GM_DISPLAYLIST, GM_DISAPPEARLIST,
       GM_FADEOUT, GM_READYGAME };
enum { MV_IDLE, MV_FOCUSIN, MV_FOCUSOUT, MV_FADEIN, MV_DETAIL };
#define GM_ICONS 56
typedef struct { LytInst *l; char gal[32], pane[16]; int status; float l_scale; Btn b; } GmIcon;
static struct {
    Nrv n, mv, list_n;
    LytInst *map, *bg, *title, *btn_l, *back_l, *detail, *plate, *list, *dome[6];
    GmIcon icon[GM_ICONS];
    int nicon, view, next_view, mode, pointing, detail_on, detail_new, list_on, page, npages, plate_show;
    Btn button, back, up, down;
    u8 back_clicked;
    u16 plate_txt[64];
    const char *list_gal[64]; int nlist;
    u16 col[5][8][128];               /* the star list's columns: stage 8 panes, star / coin / comp / time 4 */
} gm;

static int gm_status(const char *g) /* GalaxyMapIcon::examineIconStatus: 0 closed, 1 new, 2 opened, 3 completed */
{
    if (!gd_galaxy_appear(g)) return 0;
    int n = gd_power_star_num(g);
    if (n > 0 && gd_star_num_galaxy(g) == n) return 3;
    if (n == 0 || gd_flag_is_on(g)) return 2;
    return gd_flag_can_on(g) ? 1 : 0;
}

static void gm_sync_icons(void)
{
    for (int i = 0; i < gm.nicon; i++) { /* syncStatus */
        GmIcon *ic = &gm.icon[i];
        ic->status = gm_status(ic->gal);
        if (!ic->status) continue;
        lyt_start(ic->l, "Status", 0);
        lyt_set_frame(ic->l, 0, (float)ic->status); lyt_set_rate(ic->l, 0, 0);
        lyt_start(ic->l, ic->status == 1 ? "NewBlink" : "NewWait", 1);
    }
    for (int d = 0; d < 6; d++) { /* GalaxyMapDomeIcon::calcDomeStatus */
        if (!gm.dome[d]) continue;
        char key[12], grand[24];
        int complete = 1, isnew = 0;
        snprintf(key, sizeof key, "Galaxy%d", d + 1);
        for (int i = 0; i < gm.nicon; i++) {
            if (!strstr(gm.icon[i].pane, key)) continue;
            const char *g = gm.icon[i].gal;
            int n = gd_power_star_num(g);
            if (n && gd_star_num_galaxy(g) != n) complete = 0;
            if (gd_flag_can_on(g) && !gd_flag_is_on(g)) isnew = 1;
        }
        snprintf(grand, sizeof grand, "SpecialStarGrand%d", d + 1);
        int st = complete ? 3 : isnew ? 1 : gd_flag_is_on(grand) ? 2 : 0;
        char base[8];
        snprintf(base, sizeof base, "Base%d", d + 1);
        lyt_show_pane(gm.map, base, st != 0);
        lyt_start(gm.dome[d], "DomeColor", 0); lyt_set_frame(gm.dome[d], 0, (float)d);
        lyt_start(gm.dome[d], "Status", 0); lyt_set_frame(gm.dome[d], 0, (float)st); lyt_set_rate(gm.dome[d], 0, 0);
        lyt_start(gm.dome[d], "Wait", 1);
        lyt_set_alpha(gm.dome[d], st ? 1.0f : 0.0f);
    }
}

static void gm_follow(LytInst *l, const char *pane) /* setLayoutScalePosAtPaneScaleTrans (the map evaluated this frame) */
{
    float p[3];
    lyt_pane_place(gm.map, pane, p);
    lyt_set_pos(l, p[0] - 304.0f, 228.0f - p[1]);
    lyt_set_scale(l, p[2]);
}

static void gm_title(int galaxy_view)
{
    lyt_set_text_rec(gm.title, "TextTitle", msg_get(galaxy_view ? "GalaxyMap_GrandGalaxyMap" : "GalaxyMap_AstroMap"));
    lyt_start(gm.title, "TitleAppear", 0);
}

static void gm_force_view(int galaxy_view) /* forceToGalaxyMap / forceToAstroMap: the transition at its end */
{
    const char *a = galaxy_view ? "DomeIn" : "DomeOut";
    lyt_start(gm.map, a, 0);
    lyt_set_frame(gm.map, 0, lyt_anim_frames(gm.map, a)); lyt_set_rate(gm.map, 0, 0);
    gm.view = galaxy_view;
    gm_title(galaxy_view);
    gm.mv.next = -1; nrv_set(&gm.mv, MV_IDLE);
}

/* the hover plate: "?" unopened, else its short name and its stars (GalaxyMapGalaxyPlain::show) */
static void gm_plate(int i)
{
    const GmIcon *ic = &gm.icon[i];
    int n = 0;
    if (!gd_flag_is_on(ic->gal)) gm.plate_txt[n++] = '?';
    else {
        char label[48];
        snprintf(label, sizeof label, "GalaxyNameShort_%s", ic->gal);
        for (const u16 *s = msg_get(label); s && *s && n < 40; s++) gm.plate_txt[n++] = *s;
        gm.plate_txt[n++] = ' ';
        int got = gd_star_num_galaxy(ic->gal);
        if (got == gd_power_star_num(ic->gal)) { u16 t[4] = {0x1A, 3, 80, 0}; memcpy(gm.plate_txt + n, t, 8); n += 4; }
        else {
            u16 t[8] = {0x1A, 3, 55, 0, 0x1A, 3, 64, 0};
            memcpy(gm.plate_txt + n, t, 16); n += 8;
            char d[8];
            snprintf(d, sizeof d, "%d", got);
            for (char *c = d; *c; c++) { u16 nt[4] = {0x1A, 3, (u16)(0x1000 + *c), 0}; memcpy(gm.plate_txt + n, nt, 8); n += 4; }
        }
    }
    gm.plate_txt[n] = 0;
    float p[3];
    lyt_pane_place(gm.map, ic->pane, p);
    lyt_set_pos(gm.plate, p[0] - 304.0f, 228.0f - p[1]);
    lyt_show_rec(gm.plate, "GalaxyNamePlate", 1); lyt_show_rec(gm.plate, "GalaxyNamePlateU", 0);
    lyt_hide_for_good(gm.plate, "ShaBeak"); lyt_hide_for_good(gm.plate, "PicBeak");
    lyt_set_text_rec(gm.plate, "GalaxyName", gm.plate_txt);
    lyt_set_frame(gm.plate, 2, lyt_text_width(gm.plate, "TxtGalaxyName")); lyt_set_rate(gm.plate, 2, 0);
    lyt_start(gm.plate, "Unknown", 1); lyt_set_frame(gm.plate, 1, 2); lyt_set_rate(gm.plate, 1, 0);
    if (!gm.plate_show) lyt_start(gm.plate, "Appear", 0);
    gm.plate_show = 2;
}

static int gm_pointed_icon(void) /* tryFocusIn: the first icon pointed (shown, its pane not scaled to nothing) */
{
    float x, y;
    game_cursor(&x, &y);
    for (int i = 0; i < gm.nicon; i++)
        if (gm.icon[i].status && gm.icon[i].l_scale > 0.01f && lyt_pane_hit(gm.icon[i].l, "GalaxyIcon", x, y)) return i;
    return -1;
}

static void gm_map_update(void) /* GalaxyMap's nerves */
{
    switch (nrv_begin(&gm.mv)) {
    case MV_IDLE: case MV_FOCUSIN: case MV_FOCUSOUT: {
        if (!nrv_is(&gm.n, GM_DISPLAY)) break;
        int p = gm_pointed_icon();
        if (p != gm.pointing) {
            if (p >= 0) se_start(SE_SY_GALAMAP_ICON_POINT, NULL);
            gm.pointing = p;
            nrv_set(&gm.mv, p >= 0 ? MV_FOCUSIN : MV_FOCUSOUT);
        } else if (p >= 0 && pad_pressed(PAD_CROSS)) {
            pad_eat(PAD_CROSS);
            nrv_set(&gm.mv, MV_DETAIL);
        }
        if (gm.pointing >= 0) { btn_try_pointing(&gm.icon[gm.pointing].b, 1); gm.icon[gm.pointing].b.pointing = 1; gm_plate(gm.pointing); }
        break;
    }
    case MV_FADEIN:
        if (FIRST(&gm.mv)) { lyt_start(gm.map, gm.next_view ? "DomeIn" : "DomeOut", 0); gm_title(gm.next_view); }
        if (lyt_stopped(gm.map, 0)) { gm.view = gm.next_view; gm.pointing = -1; nrv_set(&gm.mv, MV_IDLE); }
        break;
    case MV_DETAIL: /* GalaxyMapGalaxyDetail */
        if (FIRST(&gm.mv)) {
            const GmIcon *ic = &gm.icon[gm.pointing];
            dmu_info_fill(gm.detail, ic->gal, ic->status == 1 ? 2 : 3);
            lyt_set_number_rec(gm.detail, "ShaCoinNum", gd_max_coins(ic->gal, 1));
            lyt_start(gm.detail, "Appear", 0);
            se_start(SE_SY_GALAMAP_WINDOW_OPEN, NULL);
            gm.detail_on = 1;
        }
        if (gm.detail_on == 1 && lyt_stopped(gm.detail, 0)) { lyt_start(gm.detail, "Wait", 0); gm.detail_on = 2; }
        if (gm.detail_on == 2 && pad_pressed(PAD_CROSS)) { pad_eat(PAD_CROSS); lyt_start(gm.detail, "End", 0); se_start(SE_SY_GALAMAP_WINDOW_CLOSE, NULL); gm.detail_on = 3; }
        if (gm.detail_on == 3 && lyt_stopped(gm.detail, 0)) { gm.detail_on = 0; nrv_set(&gm.mv, MV_IDLE); }
        break;
    }
    nrv_end(&gm.mv);
}

/* PowerStarList: the opened galaxies (sorted by their kind), a row each: short name, its stars got, best coins, crown */
static int gm_prio(const char *g)
{
    int nn = 0;
    for (int i = 1; i <= gd_scenario_num(g); i++) nn += !gd_scenario_comet(g, i)[0];
    if (nn == 3) return 1;
    if (!strcmp(g, "HeavensDoorGalaxy")) return 2;
    for (int i = 1; i <= gd_power_star_num(g); i++) if (gd_is_grand_star(g, i)) return 3;
    char ap[48];
    snprintf(ap, sizeof ap, "Appear%s", g);
    return gd_flag_known(ap) ? 4 : 5;
}
static void gm_list_build(void)
{
    gm.nlist = 0;
    for (int i = 0; i < gm.nicon && gm.nlist < 64; i++) /* (the table's order: ScenarioData's) */
        if (gd_power_star_num(gm.icon[i].gal) && gd_flag_is_on(gm.icon[i].gal)) gm.list_gal[gm.nlist++] = gm.icon[i].gal;
    for (int i = 1; i < gm.nlist; i++) /* a stable insertion sort by priority */
        for (int k = i; k > 0 && gm_prio(gm.list_gal[k]) < gm_prio(gm.list_gal[k - 1]); k--) {
            const char *t = gm.list_gal[k]; gm.list_gal[k] = gm.list_gal[k - 1]; gm.list_gal[k - 1] = t;
        }
    gm.npages = gm.nlist ? (gm.nlist - 1) / 15 + 1 : 1;
}
static void gm_col_add(u16 *col, int *n, const u16 *s, int newline)
{
    if (newline && *n < 126) col[(*n)++] = 0x0A;
    for (; s && *s && *n < 126; s++) col[(*n)++] = *s;
    col[*n] = 0;
}
static void gm_list_fill(void) /* updateList: two pages, rows 0..29 */
{
    static const char *const stage[8] = {"TxtStage1", "TxtStage2", "TxtStage3", "TxtStage4", "TxtStage5", "TxtStage6", "TxtStage7", "TxtStage8"};
    static const char *const star[4] = {"TxtStar1", "TxtStar2", "TxtStar3", "TxtStar4"}, *const coin[4] = {"TxtCoinPiece1", "TxtCoinPiece2", "TxtCoinPiece3", "TxtCoinPiece4"},
                             *const comp[4] = {"TxtComp1", "TxtComp2", "TxtComp3", "TxtComp4"}, *const tm[4] = {"TxtTime1", "TxtTime2", "TxtTime3", "TxtTime4"};
    int len[5][8];
    memset(len, 0, sizeof len);
    for (int c = 0; c < 5; c++) for (int k = 0; k < 8; k++) gm.col[c][k][0] = 0;
    int odd = gm.page % 2;
    for (int r = odd; r < 30 + odd; r++) {
        int it = gm.page * 15 + r - odd;
        if (it >= gm.nlist) break;
        const char *g = gm.list_gal[it];
        char label[48];
        u16 buf[40];
        int si = r / 4, ci = r / 8, sl = r % 4, cl = r % 8, n = 0;
        if (si >= 8 || ci >= 4) break;
        snprintf(label, sizeof label, "GalaxyNameShort_%s", g);
        gm_col_add(gm.col[0][si], &len[0][si], msg_get(label), sl != 0);
        for (int s = 1; s <= gd_power_star_num(g) && n < 36; s++)
            if (gd_has_star(g, s)) {
                int code = gd_is_grand_star(g, s) ? 'K' : gd_scenario_comet(g, s)[0] ? 'e' : '7';
                buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = (u16)(code - '0'); buf[n++] = 0;
            }
        buf[n] = 0;
        gm_col_add(gm.col[1][ci], &len[1][ci], buf, cl != 0);
        n = 0;
        buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = 'A' - '0'; buf[n++] = 0;
        int best = gd_max_coins(g, 1);
        for (int d = 100; d; d /= 10) buf[n++] = (u16)('0' + best / d % 10);
        buf[n] = 0;
        gm_col_add(gm.col[2][ci], &len[2][ci], buf, cl != 0);
        n = 0;
        if (gd_star_num_galaxy(g) == gd_power_star_num(g)) { buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = 'P' - '0'; buf[n++] = 0; }
        buf[n] = 0;
        gm_col_add(gm.col[3][ci], &len[3][ci], buf, cl != 0);
        gm_col_add(gm.col[4][ci], &len[4][ci], NULL, cl != 0);
    }
    for (int k = 0; k < 8; k++) lyt_set_text_rec(gm.list, stage[k], gm.col[0][k]);
    for (int k = 0; k < 4; k++) {
        lyt_set_text_rec(gm.list, star[k], gm.col[1][k]); lyt_set_text_rec(gm.list, coin[k], gm.col[2][k]);
        lyt_set_text_rec(gm.list, comp[k], gm.col[3][k]); lyt_set_text_rec(gm.list, tm[k], gm.col[4][k]);
    }
    for (int k = 1; k <= 4; k++) { char b[12]; snprintf(b, sizeof b, "PicBelt%d", k); lyt_show_pane(gm.list, b, 0); }
    lyt_set_text_rec(gm.list, "Title", msg_get("AllStarList_Title"));
    lyt_show_rec(gm.list, "Capture", 0); lyt_show_rec(gm.list, "Photo", 0);
    int one = gm.npages == 1; /* updateButtonAppearance */
    lyt_show_rec(gm.list, "ArrowUpButton", !one && gm.page > 0);
    lyt_show_rec(gm.list, "ArrowDownButton", !one && gm.page < gm.npages - 1);
    lyt_show_rec(gm.list, "PageNumber", !one);
    lyt_set_text_rec(gm.list, "PageNumber", NULL);
}

int menu_test_point_map(const char *pane) /* tests: the pointer on the map's select button / back button */
{
    float x, y;
    if (!lyt_pane_center(!strcmp(pane, "BoxLButton") ? gm.btn_l : gm.back_l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}
void menu_galaxy_map(int galaxy_view) /* startGalaxyMapLayout (1) / startAstroMapLayout (0) */
{
    if (!gm.map || !nrv_is(&gm.n, GM_OFF)) return;
    gm.mode = galaxy_view;
    gm.n.next = -1; nrv_set(&gm.n, GM_READY);
}
int menu_galaxy_map_active(void) { return !nrv_is(&gm.n, GM_OFF); }

static int gm_back(void) /* tryPushBackButton */
{
    if (gm.back_clicked) return 1;
    if (pad_pressed(PAD_CIRCLE) || pad_pressed(PAD_TRIANGLE)) { se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); return 1; }
    return 0;
}

static void gm_update(void)
{
    switch (nrv_begin(&gm.n)) {
    case GM_READY:
        if (FIRST(&gm.n)) wipe_start(12, 1, 0);
        if (!wipe_active()) nrv_set(&gm.n, GM_FADEIN);
        break;
    case GM_FADEIN:
        if (FIRST(&gm.n)) {
            wipe_start(12, 0, 0);
            btn_appear(&gm.back); gm.back_clicked = 0;
            btn_appear(&gm.button);
            gm_sync_icons();
            gm_force_view(gm.mode);
            lyt_start(gm.bg, "Wait", 0);
            lyt_set_number_rec(gm.map, "Star", gd_star_num());
            gm.pointing = -1; gm.list_on = 0;
        }
        if (!wipe_active()) nrv_set(&gm.n, GM_DISPLAY);
        break;
    case GM_DISPLAY:
        if (!gm.detail_on && gm_back()) { nrv_set(&gm.n, GM_FADEOUT); break; }
        if (btn_is(&gm.button, BP_DECIDEDWAIT) && gm.button.selected) { gm.button.selected = 0; nrv_set(&gm.n, GM_READYLIST); break; } /* isDecidedList */
        if (btn_pointing_trigger(&gm.button)) se_start(SE_SY_GALAMAP_CURSOR_ON, NULL);
        if (btn_try_select(&gm.button)) se_start(SE_SY_GALAMAP_DECIDE, NULL);
        else if (!gm.detail_on && gm.pointing < 0 && !pointing_on(gm.back_l, "BoxButton") && !pointing_on(gm.btn_l, "BoxLButton") &&
                 nrv_is(&gm.mv, MV_IDLE) + nrv_is(&gm.mv, MV_FOCUSOUT) && pad_pressed(PAD_CROSS)) { /* tryChangeMode */
            se_start(SE_SY_GALAMAP_PAGE_CHANGE, NULL);
            se_start(gm.view ? SE_SY_GALAMAP_CH_PITCH_DOWN : SE_SY_GALAMAP_CH_PITCH_UP, NULL);
            gm.next_view = !gm.view;
            nrv_set(&gm.mv, MV_FADEIN);
        }
        break;
    case GM_READYLIST:
        if (FIRST(&gm.n)) { wipe_start(12, 1, 0); btn_disappear(&gm.button); }
        if (!wipe_active()) nrv_set(&gm.n, GM_APPEARLIST);
        break;
    case GM_APPEARLIST:
        if (FIRST(&gm.n)) {
            wipe_start(12, 0, 0);
            gm.page = 0; gm_list_build(); gm_list_fill();
            lyt_start(gm.list, "Appear", 0);
            gm.list_on = 1;
        }
        if (!wipe_active() && lyt_stopped(gm.list, 0)) nrv_set(&gm.n, GM_DISPLAYLIST);
        break;
    case GM_DISPLAYLIST:
        if (FIRST(&gm.n)) { lyt_start(gm.list, gm.page % 2 ? "TurnOverPage2" : "TurnOverPage1", 0); lyt_set_frame(gm.list, 0, 0); lyt_set_rate(gm.list, 0, 0); }
        if (gm_back()) { gm.mode = 0; nrv_set(&gm.n, GM_DISAPPEARLIST); break; }
        if (gm.page < gm.npages - 1 && (pad_pressed(PAD_DOWN) || btn_try_select(&gm.down))) { gm.page++; se_start(SE_SY_GALAMAP_SCROLL, NULL); gm_list_fill(); }
        else if (gm.page > 0 && (pad_pressed(PAD_UP) || btn_try_select(&gm.up))) { gm.page--; se_start(SE_SY_GALAMAP_SCROLL, NULL); gm_list_fill(); }
        break;
    case GM_DISAPPEARLIST: /* back to the Astro map */
        if (FIRST(&gm.n)) wipe_start(12, 1, 0);
        if (!wipe_active()) { gm.list_on = 0; nrv_set(&gm.n, GM_FADEIN); }
        break;
    case GM_FADEOUT:
        if (FIRST(&gm.n)) wipe_start(12, 1, 0);
        if (!wipe_active()) nrv_set(&gm.n, GM_READYGAME);
        break;
    case GM_READYGAME:
        if (FIRST(&gm.n)) wipe_start(12, 0, 0);
        if (!wipe_active()) nrv_set(&gm.n, GM_OFF);
        break;
    }
    nrv_end(&gm.n);
    if (nrv_is(&gm.n, GM_OFF) || nrv_is(&gm.n, GM_READY)) return;
    if (!gm.list_on) {
        gm_map_update();
        lyt_update(gm.map); lyt_update(gm.bg); lyt_update(gm.title);
        lyt_evaluate(gm.map);
        for (int i = 0; i < gm.nicon; i++) {
            if (!gm.icon[i].status) continue;
            float p[3];
            lyt_pane_place(gm.map, gm.icon[i].pane, p);
            gm.icon[i].l_scale = p[2];
            gm_follow(gm.icon[i].l, gm.icon[i].pane); btn_update(&gm.icon[i].b); lyt_update(gm.icon[i].l);
        }
        for (int d = 0; d < 6; d++) if (gm.dome[d]) { char p[8]; snprintf(p, sizeof p, "Dome%d", d + 1); gm_follow(gm.dome[d], p); lyt_update(gm.dome[d]); }
        if (gm.plate_show) { lyt_update(gm.plate); gm.plate_show--; }
        if (gm.detail_on) lyt_update(gm.detail);
        btn_update(&gm.button); lyt_update(gm.btn_l);
    } else { btn_update(&gm.up); btn_update(&gm.down); lyt_update(gm.list); }
    if (btn_pointing_trigger(&gm.back)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
    if (btn_try_select(&gm.back)) { if (!gm.back_clicked) se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); gm.back_clicked = 1; }
    btn_update(&gm.back); lyt_update(gm.back_l);
    if (gm.back_clicked && (nrv_is(&gm.n, GM_DISPLAYLIST) || nrv_is(&gm.n, GM_DISAPPEARLIST))) gm.back_clicked = nrv_is(&gm.n, GM_DISPLAYLIST);
}

static int gm_screen(void) { return menu_galaxy_map_active() && !nrv_is(&gm.n, GM_READY) && !nrv_is(&gm.n, GM_READYGAME); }
static void gm_draw(void)
{
    if (nrv_is(&gm.n, GM_OFF) || nrv_is(&gm.n, GM_READY)) return;
    render_world_done();
    if (gm.list_on) lyt_draw(gm.list);
    else {
        lyt_draw(gm.bg); lyt_draw(gm.map);
        for (int d = 0; d < 6; d++) if (gm.dome[d]) lyt_draw(gm.dome[d]);
        for (int i = 0; i < gm.nicon; i++) if (gm.icon[i].status && gm.icon[i].l_scale > 0.01f && i != gm.pointing) lyt_draw(gm.icon[i].l);
        if (gm.pointing >= 0) lyt_draw(gm.icon[gm.pointing].l); /* order 3: over the rest */
        lyt_draw(gm.title);
        lyt_draw(gm.btn_l);
        if (gm.plate_show) lyt_draw(gm.plate);
        if (gm.detail_on) lyt_draw(gm.detail);
    }
    lyt_draw(gm.back_l);
}

static void gm_init(void)
{
    memset(&gm, 0, sizeof gm);
    gm.map = lyt_new("MapGrandGalaxy");
    if (!gm.map) return;
    gm.bg = lyt_new("MapGalaxyBg"); gm.title = lyt_new("MapTitle"); gm.btn_l = lyt_new("MapButton"); gm.back_l = lyt_new("BackButton");
    gm.detail = lyt_new("GalaxyDetail"); gm.plate = lyt_new("GalaxyNamePlate"); gm.list = lyt_new("AllStarList");
    if (!gm.bg || !gm.title || !gm.btn_l || !gm.back_l || !gm.detail || !gm.plate || !gm.list) { gm.map = NULL; return; }
    lyt_start(gm.plate, "OneLine", 2);
    btn_init(&gm.button, gm.btn_l, "MapList", "BoxLButton", 0, 1); gm.button.end_disappear = 0; /* _22 false: DecidedWait */
    btn_init(&gm.back, gm.back_l, "Back", "BoxButton", 0, 1);
    btn_init(&gm.up, gm.list, "ArrowUpButton", "BoxButton1_00", 0, 0);
    btn_init(&gm.down, gm.list, "ArrowDownButton", "BoxButton1_01", 0, 0);
    for (int i = 0; i < gd_galaxy_count() && gm.nicon < GM_ICONS; i++) { /* initPaneCtrlPointing: cGalaxyInfoTable's MapPaneName */
        const char *g, *pane;
        gd_galaxy_at(i, &g, &pane);
        if (!pane || !pane[0] || !strcmp(pane, "dummy") || lyt_pane(gm.map, pane) < 0) continue;
        GmIcon *ic = &gm.icon[gm.nicon];
        if (!(ic->l = lyt_new("MapGalaxyIcon"))) break;
        snprintf(ic->gal, sizeof ic->gal, "%s", g); snprintf(ic->pane, sizeof ic->pane, "%s", pane);
        btn_init(&ic->b, ic->l, "GalaxyIcon", "GalaxyIcon", 0, 0); ic->b.pointing_kept = 1;
        btn_appear(&ic->b);
        gm.nicon++;
    }
    for (int d = 0; d < 6; d++) gm.dome[d] = lyt_new("MapDomeIcon");
    gm.pointing = -1;
    printf("galaxy map: %d galaxy icons\n", gm.nicon);
}

/* --- MissLayout ("TooBad"), WipeKoopa, WipeGameOver ("GameOver"): the miss / game over sequences' (game.c) ----- */
static struct { LytInst *toobad, *koopa, *gameover; u8 tb_on, ko_on, go_on; } mlay;

void menu_miss_start(void) /* MissLayout::start: Appear (its TooBad effect not shown) */
{
    if (!mlay.toobad || mlay.tb_on) return;
    mlay.tb_on = 1;
    lyt_start(mlay.toobad, "Appear", 0);
}
void menu_wipe_koopa(int frames) /* WipeKoopa::wipe: "out" over `frames` (sAnimFrame 90) */
{
    if (!mlay.koopa) return;
    mlay.ko_on = 1;
    lyt_start(mlay.koopa, "out", 0);
    float end = lyt_anim_frames(mlay.koopa, "out");
    if (frames <= 0) frames = 90;
    lyt_set_rate(mlay.koopa, 0, end / frames);
}
int menu_wipe_koopa_closed(void) { return !mlay.koopa || (mlay.ko_on && lyt_stopped(mlay.koopa, 0)); } /* !isWipeActive */
void menu_wipe_gameover(void) /* WipeGameOver::exeActive: its GameOver animation */
{
    if (!mlay.gameover || mlay.go_on) return;
    mlay.go_on = 1;
    lyt_start(mlay.gameover, "GameOver", 0);
}
static void mlay_init(void)
{
    memset(&mlay, 0, sizeof mlay);
    mlay.toobad = lyt_new("TooBad");
    mlay.koopa = lyt_new("WipeKoopa");
    mlay.gameover = lyt_new("GameOver");
}
static void mlay_update(void)
{
    if (mlay.tb_on) lyt_update(mlay.toobad);
    if (mlay.ko_on) lyt_update(mlay.koopa);
    if (mlay.go_on) lyt_update(mlay.gameover);
}
static void mlay_draw(void)
{
    if (!mlay.tb_on && !mlay.ko_on && !mlay.go_on) return;
    render_world_done();
    if (mlay.tb_on) lyt_draw(mlay.toobad);
    if (mlay.ko_on) lyt_draw(mlay.koopa);
    if (mlay.go_on) lyt_draw(mlay.gameover);
}

static void wipe_update(void)
{
    if (!wipe.frames) return;
    if (wipe.t < wipe.frames) wipe.t++;
    else if (!wipe.close) wipe.frames = 0;
}

void menu_draw(void)
{
    if (sr.alive) { render_world_done(); lyt_draw(sr.l); }
    if (sv.win.alive) { render_world_done(); lyt_draw(sv.win.l); }
    if (dmu.on) { render_world_done(); dmu_draw(); }
    mlay_draw();
    gm_draw();
    wipe_draw_quad();
    fade_draw();
    if (!pm.alive) return;
    render_world_done(); /* the scene under the menu (LayoutOnPause draws after everything) */
    lyt_draw(pm.l);
    if (pm.win.alive) lyt_draw(pm.win.l);

}
