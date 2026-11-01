#include "menu.h"

#include "anim.h"
#include "game.h"
#include "gamedata.h"
#include "sequence.h"
#include "lyt.h"
#include "mc.h"
#include "me.h"
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "se.h"
#include "se_ids.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void fade_update(void);
static void msg_with_args(u16 *out, int cap, const u16 *src, const u16 *const *args, int nargs);
static int sv_prompt; /* a stage clear left its result for this scene (the save after it: game.c story_update) */
static void fade_draw(void);
static void ss_init(const Scene *s);
static void dmu_init(void);
static void dmu_update(void);
static void mlay_init(void);
static void mono_update(void);
static void srl_init(void);
static void srl_update(void);
static void srl_draw(void);
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
static void hm_init(void);
static void mlay_draw(void);
static void ss_update(void);
static void wipe_update(void);
static void wipe_draw_quad(void);

/* the screen wipe of the select (closeSystemWipeWhiteFade / closeSystemWipeFadeWithCaptureScreen and the opens) */
static struct { int t, frames, close; u32 rgb; } wipe;
static void wipe_start(int frames, int close, u32 rgb) { wipe.t = 0; wipe.frames = frames; wipe.close = close; wipe.rgb = rgb; }
static int wipe_active(void) { return wipe.frames && wipe.t < wipe.frames; }
static struct { LytInst *l; int on, no; const u16 *page; u16 buf[400]; } mono; /* RosettaMonologue (by wipe_update) */
struct Iab;
static struct Iab *mono_ab(void);

/* --- messages (tools/lyt.py MSGS) ------------------------------------------------------------------------------ */
static const u8 *msgs;
void msg_set(const u8 *m) { msgs = m; }

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
 * game goes into its file (gd_file_save) and the files to the memory card (main.c, mc.c). executeSaveFinish's
 * failure and the boot's PreLoad (mc_take_boot, the first scene with the window) run NANDErrorSequence's windows
 * (SV_E_*): a corrupted save asks NAND_07 (Yes: removed, System_Save05; No: System_Save04, then no saving), no room
 * asks NAND_02 (Yes: out to the menu; No: System_Save04, then no saving), a failed write NAND_11_2 then the menu.
 * ponytail: the Wii's texts kept (Wii system memory, the Wii Menu: the disc has no PS2 ones); no card or an
 * unformatted one is NAND_02's (the Wii always has its memory); a save's NOCARD / FULL also go to NAND_02 (the Wii
 * checks room once, at boot: a PS2 card can be pulled out between); the Wii Menu is the PS2 browser ----------------- */
enum { SV_OFF, SV_CONFIRM, SV_SAVE, SV_WINDOWOUT, SV_DONE, SV_REMIND,
       SV_E_ERROR, SV_E_REMOVECONFIRM, SV_E_REMOVE, SV_E_REMOVEDONE, SV_E_NOSAVEREMOVE, SV_E_NOSAVELESS,
       SV_E_LESSCONFIRM, SV_E_GOMENU, SV_E_CHECK };
static struct { Nrv n; SysInfo win; int remind, quit, result; u32 lv; const char *msg; } sv;
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
static int sv_confirm(const char *label, int *yes) /* tryConfirm */
{
    if (FIRST(&sv.n)) { si_appear(&sv.win, label, SI_YESNO); se_start(SE_SY_SAVE_CONFIRM_INFO, NULL); }
    if (sv.win.alive || sv.n.step == 0) return 0;
    *yes = yn_yes(&sv.win.yn);
    return 1;
}
static int sv_key(const char *label) /* tryProcessDoneKeyWait */
{
    if (FIRST(&sv.n)) si_appear(&sv.win, label, SI_KEY);
    return !sv.win.alive && sv.n.step > 0;
}
static void sv_error(int res) /* the NANDErrorSequence a card result starts (exePreLoad, executeSaveFinish) */
{
    if (res == MC_CORRUPT) nrv_set(&sv.n, SV_E_REMOVECONFIRM); /* isSaveDataCorrupted: startRemoveFile */
    else if (res == MC_FAILED) { sv.msg = "NAND_11_2"; nrv_set(&sv.n, SV_E_ERROR); } /* startErrorToWiiMenu */
    else if (res == MC_NOCARD || res == MC_FULL) { sv.msg = "NAND_02"; nrv_set(&sv.n, SV_E_LESSCONFIRM); } /* tryLessBlockOrInode */
}
static void sv_error_end(int nosave) /* the error sequence killed: syncNoSaveFlagsFromErrorSequence, NoOperation */
{
    if (nosave) mc_set_nosave();
    sv.result = 3;
    nrv_set(&sv.n, SV_OFF);
}
static void sv_take_errors(void) /* the boot's PreLoad result; a save SaveAllWithoutKeyWait made (file select) failed */
{
    if (!sv.win.l || menu_saving()) return;
    int res = mc_take_boot();
    if (res == MC_OK || res == MC_NOFILE) {
        res = mc_take_error();
        if (res) se_start(SE_SY_SAVE_FAILURE, NULL);
    }
    if (res == MC_OK || res == MC_NOFILE) return;
    sv.result = 0;
    sv.n.next = -1;
    sv_error(res);
}
static void sv_update(void)
{
    int yes = 0;
    if (!menu_saving()) return;
    switch (nrv_begin(&sv.n)) {
    case SV_CONFIRM:
        if (mc_nosave()) { nrv_set(&sv.n, SV_SAVE); break; } /* (_2C || _2B) */
        if (!sv_confirm(sv.quit ? "System_Save07" : "System_Save00", &yes)) break;
        if (yes) nrv_set(&sv.n, SV_SAVE);
        else if (sv.remind) nrv_set(&sv.n, SV_REMIND);
        else { sv.result = 3; nrv_set(&sv.n, SV_OFF); }
        break;
    case SV_SAVE: /* trySave */
        if (FIRST(&sv.n)) gd_file_save();
        if (mc_nosave()) { sv.result = 2; nrv_set(&sv.n, SV_OFF); break; } /* tryNoSave */
        if (FIRST(&sv.n)) { menu_save_request(); si_appear(&sv.win, "System_Save01", SI_BLOCKING); }
        if (sv.n.step > 20) sv.lv = se_level(SE_SY_LV_SAVING, NULL, sv.lv);
        if (sv.n.step > 120 && !mc_busy()) nrv_set(&sv.n, SV_WINDOWOUT);
        break;
    case SV_WINDOWOUT: /* trySaveWindowDisappear */
        if (FIRST(&sv.n)) si_disappear(&sv.win);
        if (sv.win.alive) break;
        mc_take_error(); /* (this save's: executeSaveFinish) */
        if (mc_result() == MC_OK) { se_start(SE_SY_SAVE_SUCCESS, NULL); nrv_set(&sv.n, SV_DONE); }
        else { se_start(SE_SY_SAVE_FAILURE, NULL); sv_error(mc_result()); }
        break;
    case SV_DONE:
        if (sv_key("System_Save02")) { sv.result = 2; nrv_set(&sv.n, SV_OFF); }
        break;
    case SV_REMIND:
        if (!sv_confirm("ConfirmEndGame", &yes)) break;
        if (yes) { sv.result = 2; nrv_set(&sv.n, SV_OFF); }
        else nrv_set(&sv.n, SV_CONFIRM);
        break;
    /* NANDErrorSequence */
    case SV_E_ERROR: /* exeError */
        if (sv_key(sv.msg)) mc_exit_to_browser(); /* requestGoWiiMenu */
        break;
    case SV_E_REMOVECONFIRM:
        if (!sv_confirm("NAND_07", &yes)) break;
        nrv_set(&sv.n, yes ? SV_E_REMOVE : SV_E_NOSAVEREMOVE);
        break;
    case SV_E_REMOVE: /* requestRemoveSaveData (blocking) */
        mc_remove();
        nrv_set(&sv.n, SV_E_REMOVEDONE);
        break;
    case SV_E_REMOVEDONE:
        if (sv_key("System_Save05")) nrv_set(&sv.n, SV_E_CHECK);
        break;
    case SV_E_NOSAVEREMOVE: /* exeNoSaveConfirmBeforeRemove: result 3, isResultFileCorrupted */
        if (!sv_confirm("System_Save04", &yes)) break;
        if (yes) sv_error_end(1);
        else nrv_set(&sv.n, SV_E_REMOVECONFIRM);
        break;
    case SV_E_NOSAVELESS: /* exeNoSaveConfirmLessBlockOrInode: result 2, isResultNoSave */
        if (!sv_confirm("System_Save04", &yes)) break;
        if (yes) sv_error_end(1);
        else nrv_set(&sv.n, SV_E_LESSCONFIRM);
        break;
    case SV_E_LESSCONFIRM:
        if (!sv_confirm(sv.msg, &yes)) break;
        nrv_set(&sv.n, yes ? SV_E_GOMENU : SV_E_NOSAVELESS);
        break;
    case SV_E_GOMENU:
        if (!sv.win.alive) mc_exit_to_browser(); /* requestGoWiiMenu */
        break;
    case SV_E_CHECK: { /* exeCheckEnableToCreateAfterRemove: tryCheckEnableToCreate (blocking) */
        int res = mc_check();
        if (res == MC_NOFILE) sv_error_end(0);
        else sv_error(res);
        break;
    }
    }
    nrv_end(&sv.n);
    si_update(&sv.win);
}

/* --- StageResultInformer ("ResultWindow") after the stage clear's return: the star got (System_Result000 / 003 with
 * the galaxy and scenario names, the star count counted up after 85 frames), a new coin record (System_Result006),
 * the galaxy's star pieces counted into the stock (System_Result004); X goes on; a galaxy just openable
 * (System_Result001) or a hungry Luma's galaxy just appeared (System_Result007): DisplayInformationForResult's window,
 * then the map in its discover mode (GalaxyMapController; the informer waits while it shows). ponytail: no galaxy
 * complete / picture book windows, no IconAButton, no effects or BGM (startSubBGM BGM_NEW_GALAXY); the information
 * window (InformationWindow) and the map show only where their packs are loaded (ending.bin / map.bin) */
enum { SR_OFF, SR_APPEARSTAR, SR_WAITCOUNT, SR_COUNT, SR_DISPLAY, SR_DISAPPEAR, SR_APPEARSCORE, SR_APPEARPIECE, SR_DISPLAYPIECE,
       SR_WAITPIECE, SR_INFO, SR_INFOEND, SR_SHOWMAP };
static struct { LytInst *l, *info; Nrv n; int kind, pieces, pressed, alive, delay, held, demo, map_mode; const char *info_msg; SeqResult r; u16 text[160]; } sr;

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
    else if (sr.kind < 3 && gd_just_open_galaxy()) { /* ShowNewGalaxyDiscover, ShowGalaxyMap */
        sr.kind = 3; sr.info_msg = "System_Result001"; sr.map_mode = GMAP_NEW_GALAXY; nrv_set(&sr.n, sr.info ? SR_INFO : SR_SHOWMAP);
    } else if (sr.kind < 5 && gd_just_tico_galaxy()) { /* ShowTicoGalaxyAppear, ShowAstroMapForTico */
        sr.kind = 5; sr.info_msg = "System_Result007"; sr.map_mode = GMAP_NEW_TICO; nrv_set(&sr.n, sr.info ? SR_INFO : SR_SHOWMAP);
    } else { sr.alive = 0; nrv_set(&sr.n, SR_OFF); }
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
    case SR_INFO: /* DisplayInformationForResult: appearInformationMessageCenter, Display after 15 (decide: Disappear) */
        if (FIRST(&sr.n)) {
            lyt_show_rec(sr.info, "InfoWindowUp", 0); lyt_show_rec(sr.info, "InfoWindowCenter", 1); /* setCenter(true) */
            lyt_set_text_rec(sr.info, "InfoWindowCenter", msg_get(sr.info_msg));
            lyt_start(sr.info, "Appear", 0);
            lyt_start(sr.info, "Line", 1);
            lyt_set_frame(sr.info, 1, lyt_text_height(sr.info, "InfConfirmC")); /* setAnimFrameAndStopAdjustTextHeight */
            lyt_set_rate(sr.info, 1, 0);
        }
        if (sr.n.step >= 15 && pad_pressed(PAD_CROSS)) { se_start(SE_SY_TALK_OK, NULL); nrv_set(&sr.n, SR_INFOEND); } /* sWaitAppearInformationWindow */
        break;
    case SR_INFOEND: /* disappearInformationMessage, isDeadInformationMessage */
        if (FIRST(&sr.n)) lyt_start(sr.info, "End", 0);
        if (sr.n.step > 0 && lyt_stopped(sr.info, 0)) nrv_set(&sr.n, SR_SHOWMAP);
        break;
    case SR_SHOWMAP: /* startAstroMapLayoutForNewGalaxyDiscover / ForNewTicoGalaxyDiscover, then decideNextNerve */
        if (FIRST(&sr.n)) menu_galaxy_map(sr.map_mode);
        else sr_next();
        break;
    }
    nrv_end(&sr.n);
    lyt_update(sr.l);
    if (sr.info && (nrv_is(&sr.n, SR_INFO) || nrv_is(&sr.n, SR_INFOEND))) lyt_update(sr.info);
}
int menu_result_showing(void) { return sr.alive || sr.delay || sr.held; }
void menu_result_start(void) /* StarReturnDemoStarter::tryStartStageResult: the held result window appears */
{
    if (!sr.held || !sr.l) return;
    sr.held = 0; sr.alive = sr.demo = 1; sr.n.next = -1; nrv_set(&sr.n, SR_APPEARSTAR);
}

/* --- IconAButton (Screen/IconAButton.cpp, menus.bin's IconAButton): Open (Appear), Wait (Press), Term (End, dead);
 * openWithoutMessage hides PicPlate and clears its text; placed at the host's pane (setFollowActorPane /
 * setLayoutPosAtPaneTrans: copyPaneTrans each frame) */
enum { IAB_OPEN, IAB_WAIT, IAB_TERM };
typedef struct Iab { LytInst *l, *host; const char *pane; Nrv n; int on; float pos[2]; } Iab;
static void iab_init(Iab *b, LytInst *host, const char *pane)
{
    memset(b, 0, sizeof *b);
    b->host = host; b->pane = pane; b->n.next = -1;
    if (!(b->l = lyt_new("IconAButton"))) return;
    lyt_start(b->l, "Appear", 0);
    lyt_set_frame(b->l, 0, 0.0f); lyt_set_rate(b->l, 0, 0.0f); /* setAnimFrameAndStop(0) */
    lyt_follow(b->l, "RootPane", b->pos, 0);
}
static void iab_open(Iab *b) /* openWithoutMessage */
{
    static const u16 none[1];
    if (!b->l) return;
    b->on = 1;
    lyt_copy_pane_trans(b->host, b->pane, b->pos); /* updateFollowPos */
    lyt_show_pane(b->l, "PicPlate", 0);
    lyt_set_text_rec(b->l, "IconAButton", none);
    nrv_set(&b->n, IAB_OPEN);
}
static int iab_wait(const Iab *b) { return b->on && nrv_is(&b->n, IAB_WAIT); }
static void iab_term(Iab *b) { if (b->on && (nrv_is(&b->n, IAB_WAIT) || nrv_is(&b->n, IAB_OPEN))) nrv_set(&b->n, IAB_TERM); }
static void iab_update(Iab *b) /* movement: the nerve, then control (updateFollowPos) */
{
    if (!b->on) return;
    switch (nrv_begin(&b->n)) {
    case IAB_OPEN:
        if (FIRST(&b->n)) lyt_start(b->l, "Appear", 0);
        if (lyt_stopped(b->l, 0)) nrv_set(&b->n, IAB_WAIT);
        break;
    case IAB_WAIT:
        if (FIRST(&b->n)) lyt_start(b->l, "Press", 0);
        break;
    case IAB_TERM:
        if (FIRST(&b->n)) lyt_start(b->l, "End", 0);
        if (lyt_stopped(b->l, 0)) b->on = 0; /* kill */
        break;
    }
    nrv_end(&b->n);
    lyt_copy_pane_trans(b->host, b->pane, b->pos);
    lyt_update(b->l);
}
static void iab_draw(Iab *b) { if (b->on) lyt_draw(b->l); }

/* --- Luigi: LuigiLetter, the pause menu's letter button, BrosButton and FileSelectInfo's CharaState after Petari -----
 * LuigiLetter (letter.bin, main.c loads it where the letter can show): its galaxy's picture (LuigiPicture<galaxy>, a
 * material of letter.bin) on PicDummy; Appear (SE_SY_LETTER_APPEAR, then SE_SV_LUIGI_MAIL), Wait until A
 * (SE_SY_TALK_OK), End, dead. The pause menu's (createLuigiLetterForMenu: getLuigiLetterGalaxyName) and
 * KinopioAstro's (createLuigiLetterForTalk: ...ForNPC, game.c kast_event) are drawn over everything.
 * Its IconAButton at AButtonPosition (openWithoutMessage in Wait, A only once it waits, term at End).
 * ponytail: no CS_CLICK_CLOSE (the remote's speaker) */
enum { LL_APPEAR, LL_WAIT, LL_END };
static struct { LytInst *l; int on; Nrv n; Iab ab; } llt;

void menu_luigi_letter(const char *galaxy, int talk)
{
    (void)talk; /* (the talk's layout scene or the pause's: both drawn by menu_draw) */
    if (!galaxy || !(llt.l || (llt.l = lyt_new("LuigiLetter")))) return;
    char mn[24];
    snprintf(mn, sizeof mn, "%.20s", galaxy); /* "LuigiPicture%s.bti" (lyt.py LETTER: the material by its galaxy) */
    lyt_replace_pane_texture(llt.l, "PicDummy", lyt_material_texture(llt.l, mn));
    if (!llt.ab.l) iab_init(&llt.ab, llt.l, "AButtonPosition");
    llt.on = 1; llt.n.next = -1; nrv_set(&llt.n, LL_APPEAR);
}
int menu_luigi_letter_alive(void) { return llt.on; }
int menu_test_letter(void) { return llt.on * 10 + (llt.ab.on ? llt.ab.n.cur + 1 : 0); } /* tests: letter on, its IconAButton nerve + 1 */
static void llt_update(void)
{
    if (!llt.on) return;
    switch (nrv_begin(&llt.n)) {
    case LL_APPEAR:
        if (FIRST(&llt.n)) { lyt_start(llt.l, "Appear", 0); se_start(SE_SY_LETTER_APPEAR, NULL); }
        if (lyt_stopped(llt.l, 0)) { se_start(SE_SV_LUIGI_MAIL, NULL); nrv_set(&llt.n, LL_WAIT); }
        break;
    case LL_WAIT:
        if (FIRST(&llt.n)) { lyt_start(llt.l, "Wait", 0); iab_open(&llt.ab); }
        if ((iab_wait(&llt.ab) || !llt.ab.l) && pad_pressed(PAD_CROSS)) { /* isWait, testCorePadButtonA */
            pad_eat(PAD_CROSS); se_start(SE_SY_TALK_OK, NULL); nrv_set(&llt.n, LL_END);
        }
        break;
    case LL_END:
        if (FIRST(&llt.n)) { lyt_start(llt.l, "End", 0); iab_term(&llt.ab); }
        if (lyt_stopped(llt.l, 0)) llt.on = 0; /* kill */
        break;
    }
    nrv_end(&llt.n);
    if (llt.on) { lyt_update(llt.l); iab_update(&llt.ab); } /* movement: the letter, then its icon */
}
static void llt_draw(void) { if (llt.on) { render_world_done(); lyt_draw(llt.l); iab_draw(&llt.ab); } }

/* PauseMenu's letter (_38: ButtonPaneController LetterButton / BoxButton3): there while getLuigiLetterGalaxyName,
 * appearing while isOnLuigiHiding; decided: exeLuigiLetter (the letter until it is dead, then the buttons back) */
static struct { Btn b; u8 on, open; } pml;
static void pml_stage(char *st, int n, int *scen)
{
    const char *sl = strchr(game_stage_name, '/');
    snprintf(st, n, "%.*s", sl ? (int)(sl - game_stage_name) : (int)strlen(game_stage_name), game_stage_name);
    *scen = sl ? atoi(sl + 1) : -1;
}
static int pml_init(LytInst *pause) /* 0: no letter (hidePaneRecursive "LetterButton") */
{
    char st[48];
    int scen;
    memset(&pml, 0, sizeof pml);
    pml_stage(st, sizeof st, &scen);
    if (!gd_luigi_letter_galaxy(st, scen) || !(llt.l || (llt.l = lyt_new("LuigiLetter")))) return 0;
    btn_init(&pml.b, pause, "LetterButton", "BoxButton3", 0, 1);
    pml.b.end_disappear = 0; /* _22 = false */
    pml.on = 1;
    return 1;
}
static void pml_appear(void)
{
    char st[48];
    int scen;
    pml_stage(st, sizeof st, &scen);
    if (pml.on && gd_luigi_hiding(st, scen)) btn_appear(&pml.b); /* MR::isOnLuigiHiding */
}
static int pml_shown(void) { return pml.on && !btn_hidden(&pml.b); }
int menu_test_pml(void) { return pml.on * 10 + pml_shown(); } /* tests: the letter button made (10), shown (1) */
static int pml_try_select(void) { return pml_shown() && btn_try_select(&pml.b); }
static int pml_decided(void) /* exeDecided's letter part: 1 once it is decided and waits (exeLuigiLetter next) */
{
    if (!pml_shown() || !pml.b.selected) return 0;
    if (btn_selected_se(&pml.b)) se_start(SE_SY_FILE_SEL_UPPER_DECIDE, NULL);
    return btn_decided_wait(&pml.b);
}
static int pml_letter(void) /* exeLuigiLetter: 1 while the letter is up */
{
    char st[48];
    int scen;
    if (!pml.open) { pml_stage(st, sizeof st, &scen); pml.open = 1; menu_luigi_letter(gd_luigi_letter_galaxy(st, scen), 0); }
    if (llt.on) return 1;
    pml.open = 0;
    return 0;
}

/* --- PauseMenu ------------------------------------------------------------------------------------------------- */
enum { PM_SELECTING, PM_DECIDED, PM_DISAPPEAR, PM_CONFIRM, PM_SAVE, PM_LETTER };
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
    if (!pml_init(pm.l)) lyt_show_rec(pm.l, "LetterButton", 0); /* MR::getLuigiLetterGalaxyName: LuigiLetter, its button */
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
    pml_appear();
    nrv_set(&pm.n, PM_SELECTING);
    pm.alive = 1;
    se_start(SE_SY_PAUSE_ON, NULL);
}

static void pm_update(void)
{
    switch (nrv_begin(&pm.n)) {
    case PM_SELECTING:
        if (btn_pointing_trigger(&pm.up) || btn_pointing_trigger(&pm.down) || (pml_shown() && btn_pointing_trigger(&pml.b)))
            se_start(SE_SY_SELECT_PAUSE_ITEM, NULL);
        if (btn_try_select(&pm.up) || btn_try_select(&pm.down) || pml_try_select()) { nrv_set(&pm.n, PM_DECIDED); break; }
        if (pm_stopped_without_button() && !btn_appearing(&pm.up) && !btn_appearing(&pm.down) &&
            (!pml_shown() || !btn_appearing(&pml.b)) && pad_pressed(PAD_START)) {
            se_start(SE_SY_PAUSE_OFF, NULL); /* Plus / Minus: back to the game */
            nrv_set(&pm.n, PM_DISAPPEAR);
        }
        break;
    case PM_DECIDED:
        if (pm.up.selected) {
            if (btn_selected_se(&pm.up)) se_start(SE_SY_PAUSE_OFF, NULL);
            if (btn_decided_wait(&pm.up)) nrv_set(&pm.n, PM_DISAPPEAR);
        } else if (pm.down.selected && btn_decided_wait(&pm.down)) nrv_set(&pm.n, pm_invalid_back() ? PM_SAVE : PM_CONFIRM);
        else if (pml_decided()) nrv_set(&pm.n, PM_LETTER);
        break;
    case PM_LETTER: /* exeLuigiLetter */
        if (!pml_letter()) { btn_force_wait(&pm.up); btn_force_wait(&pm.down); if (pml_shown()) btn_force_wait(&pml.b); nrv_set(&pm.n, PM_SELECTING); }
        break;
    case PM_DISAPPEAR:
        if (FIRST(&pm.n)) { pm_anim_without_button("End"); btn_disappear(&pm.up); btn_disappear(&pm.down); if (pml_shown()) btn_disappear(&pml.b); }
        if (pm_stopped_without_button() && btn_hidden(&pm.up) && btn_hidden(&pm.down) && !pml_shown()) pm.alive = 0;
        break;
    case PM_SAVE: /* exeGameDataSave: startGameDataSaveSequence(false, true) */
        if (FIRST(&pm.n)) menu_save_start(1, 0, 1);
        else if (!menu_saving()) nrv_set(&pm.n, PM_CONFIRM);
        break;
    case PM_CONFIRM:
        if (FIRST(&pm.n)) si_appear(&pm.win, pm_invalid_back() ? "PauseMenu_ConfirmEndGame" : "PauseMenu_ConfirmBackAstroDome", SI_YESNO);
        if (!pm.win.alive && !pm.fade) {
            if (yn_yes(&pm.win.yn)) pm.fade = 1; /* closeSystemWipeFadeWithCaptureScreen(90), then the scene */
            else { btn_force_wait(&pm.up); btn_force_wait(&pm.down); if (pml_shown()) btn_force_wait(&pml.b); nrv_set(&pm.n, PM_SELECTING); }
        }
        break;
    }
    nrv_end(&pm.n);
    btn_update(&pm.up);
    btn_update(&pm.down);
    if (pml.on) btn_update(&pml.b);
    lyt_update(pm.l);
    si_update(&pm.win);
    if (pm.fade == 1) { /* closeSystemWipeFadeWithCaptureScreen(90), then requestChangeSceneTitle / ...GoBackAstroDome */
        pm.fade = 0;
        menu_fade_move(90, pm_invalid_back() ? SEQ_TITLE : SEQ_GALAXY_OUT, NULL, 1, -1, 0);
    }
}

/* --- StarPointerGuidance after Petari (Screen/StarPointerGuidance.cpp): the PointerGuidance layout's NPointerGuide,
 * the 1P guidance message (request1PGuidance each frame: shown after hRequestTime 30 frames of the same message,
 * EndOneLine / EndTwoLine hNotRequestEndTime 30 frames after the last request; with the timeout its Wait ends it at
 * hEndTimeOutHide 240). ponytail: PointerFrame1 / PointerFrame2 (the remote out of the screen, CS_NOTICE_USE_DPD)
 * never show: the PS2's stick pointer never leaves the screen */
enum { SPG_ENDWAIT, SPG_APPEAR, SPG_WAIT, SPG_END };
static struct { LytInst *l; Nrv n; int time, active, disabled, timeout, lines; const u16 *msg, *prev; } spg;
static void spg_init(void)
{
    memset(&spg, 0, sizeof spg);
    spg.n.next = -1; spg.lines = 1;
    if (!(spg.l = lyt_new("PointerGuidance"))) return; /* (menus.bin packed without it: no guidance) */
    lyt_show_rec(spg.l, "NPointerGuide", 0);
    lyt_show_rec(spg.l, "PointerFrame1", 0);
    lyt_show_rec(spg.l, "PointerFrame2", 0);
}
int menu_guidance_request(const char *label, int timeout)
{
    if (!spg.l) return 0;
    spg.msg = label ? msg_get(label) : NULL;
    spg.active = 1; spg.timeout = timeout;
    return 1;
}
int menu_guidance_exists(void) { return !nrv_is(&spg.n, SPG_ENDWAIT) && !spg.disabled; } /* isExistGuidanceOrFrame */
static void spg_update(void) /* control: the guidance's spine, then checkRequest1P */
{
    static const u16 empty[1];
    if (!spg.l) return;
    const char *two = spg.lines >= 2 ? "TwoLine" : "OneLine";
    char an[16];
    switch (nrv_begin(&spg.n)) {
    case SPG_APPEAR: /* exeAppearCore */
        if (FIRST(&spg.n)) { lyt_show_rec(spg.l, "NPointerGuide", 1); snprintf(an, sizeof an, "Appear%s", two); lyt_start_pane(spg.l, "NPointerGuide", an, 0); }
        else if (lyt_pane_stopped(spg.l, "NPointerGuide", 0)) nrv_set(&spg.n, SPG_WAIT);
        break;
    case SPG_WAIT: /* exeWaitCore */
        if (FIRST(&spg.n)) { spg.disabled = 0; lyt_start_pane(spg.l, "NPointerGuide", "Wait", 0); }
        else if (spg.timeout && spg.n.step == 240) { /* hEndTimeOutHide */
            spg.disabled = 1; snprintf(an, sizeof an, "End%s", two); lyt_start_pane(spg.l, "NPointerGuide", an, 0);
        }
        break;
    case SPG_END: /* exeEndCore */
        if (FIRST(&spg.n)) { if (!spg.disabled) { snprintf(an, sizeof an, "End%s", two); lyt_start_pane(spg.l, "NPointerGuide", an, 0); } }
        else if (lyt_pane_stopped(spg.l, "NPointerGuide", 0)) { lyt_show_rec(spg.l, "NPointerGuide", 0); nrv_set(&spg.n, SPG_ENDWAIT); }
        break;
    }
    nrv_end(&spg.n);
    if (!spg.active) { /* checkRequest1P */
        spg.time = spg.time < 0 ? spg.time - 1 : -1;
        spg.prev = spg.msg; spg.msg = NULL;
        if (spg.time <= -30 && nrv_is(&spg.n, SPG_WAIT)) nrv_set(&spg.n, SPG_END); /* hNotRequestEndTime */
    } else {
        if (spg.time < 0) spg.time = 0;
        spg.time++;
        if (spg.msg) {
            if (spg.msg != spg.prev) spg.time = 0;
            if (spg.time > 30 && nrv_is(&spg.n, SPG_ENDWAIT)) { /* hRequestTime */
                spg.lines = 1;
                for (const u16 *s = spg.msg; *s; s++) spg.lines += *s == 10; /* countMessageLine ('\n') */
                lyt_set_text_rec(spg.l, "NPointerGuide", spg.msg);
                nrv_set(&spg.n, SPG_APPEAR);
            }
        } else if (nrv_is(&spg.n, SPG_WAIT)) { lyt_set_text_rec(spg.l, "NPointerGuide", empty); nrv_set(&spg.n, SPG_END); }
        spg.active = 0; spg.prev = spg.msg;
    }
    lyt_update(spg.l);
}

/* --- GamePauseSequence ----------------------------------------------------------------------------------------- */
static char select_after[32]; /* MoviePlayingSequence (FinalBattle): the scenario select opened over the stage */
static void subm_init(void);
void menu_init(const Scene *s)
{
    msgs = s->msgs;
    pm_init();
    ss_init(s);
    si_init(&sv.win);
    sv_prompt = seq_take_save_prompt();
    memset(&sr, 0, sizeof sr);
    subm_init();
    hm_init();
    sr.l = lyt_new("ResultWindow");
    sr.info = lyt_new("InformationWindow"); /* MR::appearInformationMessageCenter's (NULL where not loaded) */
    if (sv_prompt && seq_take_result(&sr.r)) { /* a star return demo shows it at its wait's end (menu_result_start) */
        const SeqDemo *d = seq_demo_current();
        int ret = d && d->type == 0 && strstr(d->name, "帰還"); /* 天文ドームスター帰還, パワースター帰還, グランドスター<n>帰還 */
        if (ret) sr.held = 1; else sr.delay = 60;
    }
    dmu_init();
    mlay_init();
    memset(&mono, 0, sizeof mono);
    mono.l = lyt_new("PrologueStarSteward"); /* in map.bin: the Observatory only */
    srl_init();
    spg_init();
    fs_init(s);
    gm_init();
    u32 rgb;
    int open = seq_take_open_wipe(&rgb); /* the wipe the last scene closed opens on this one */
    if (open) wipe_start(open, 0, rgb);
    seq_take_select_after(select_after, sizeof select_after);
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

/* MarioSubMeter / SubMeterLayout: FlyMeter (ハチメーター, the bee's flight) and AirMeter (水中メーター, the oxygen), one
 * active at a time (activeSubMeterBee / Water, frameOutSubMeter); its Count pane at 128 - 128 x ratio; Appear, Wait,
 * Danger under 64 (its rate 1 + (64 - n) / 32), End; full and Mario on the ground (or at the surface) 60 frames: End,
 * back with Appear when he leaves it; 60 right and 70 up of Mario on the screen (608 x 456) */
static struct { LytInst *l; int st, n, cnt, shown; } subm[2];
static int subm_active = -1, subm_vis;
static void subm_init(void)
{
    static const char *const name[2] = {"FlyMeter", "AirMeter"};
    subm_active = -1;
    for (int k = 0; k < 2; k++) {
        subm[k].l = lyt_new(name[k]);
        subm[k].st = subm[k].cnt = subm[k].shown = 0; subm[k].n = 128;
        if (!subm[k].l) continue;
        lyt_start_pane(subm[k].l, "Count", "Count", 0);
        lyt_set_pane_frame_stop(subm[k].l, "Count", 0, 0.0f);
    }
}
static void subm_appear(int k) /* requestAppear / requestFrameIn */
{
    if (!subm[k].l) return;
    subm[k].shown = 1; lyt_start(subm[k].l, "Appear", 0); subm[k].st = 1;
}
static void subm_deactivate(int k) { subm[k].shown = 0; subm[k].st = 0; } /* requestDeactivate: hideLayout */
void menu_submeter_mode(int mode) /* 0 frameOutSubMeter (ground), 1 activeSubMeterWater, 2 activeSubMeterBee */
{
    if (mode == 0) {
        if (subm_active >= 0 && subm[subm_active].l) { lyt_start(subm[subm_active].l, "End", 0); subm[subm_active].st = 4; } /* requestFrameOut */
        subm_active = -1;
        return;
    }
    int k = mode == 2 ? 0 : 1;
    if (subm_active == k) return;
    if (subm_active >= 0) subm_deactivate(subm_active);
    subm_appear(k);
    subm_active = k;
}
void menu_submeter_ratio(int bee, float ratio) /* setLifeRatio */
{
    int k = bee ? 0 : 1;
    subm[k].n = (int)(128.0f * ratio);
    if (subm[k].l) lyt_set_pane_frame_stop(subm[k].l, "Count", 0, (float)(0x80 - subm[k].n));
}
void menu_submeter_update(int resting, float x, float y, int visible) /* control (Mario on the ground / at the surface; his
                                                                       * screen place; the default game layout on) */
{
    subm_vis = visible;
    for (int k = 0; k < 2; k++) {
        LytInst *l = subm[k].l;
        if (!l) continue;
        switch (subm[k].st) {
        case 1: if (lyt_stopped(l, 0)) { lyt_start(l, "Wait", 0); subm[k].cnt = 0; subm[k].st = 2; } break;
        case 2: if (subm[k].n < 0x40) { lyt_start(l, "Danger", 0); subm[k].st = 3; } break;
        case 3:
            if (subm[k].n > 0x40) { lyt_start(l, "Wait", 0); lyt_set_rate(l, 0, 1.0f); subm[k].st = 2; }
            else lyt_set_rate(l, 0, (0x40 - subm[k].n) / 32.0f + 1.0f);
            break;
        case 4: if (lyt_stopped(l, 0)) { subm[k].shown = 0; subm[k].st = 0; } break;
        }
        if (resting) {
            if ((subm[k].st == 2 || subm[k].st == 3) && subm[k].n == 0x80 && ++subm[k].cnt == 60) { lyt_start(l, "End", 0); subm[k].st = 5; }
        } else if (subm[k].st == 5) { subm[k].st = 1; subm[k].cnt = 0; lyt_start(l, "Appear", 0); }
        if (subm[k].shown) {
            float sx = x * (608.0f / 640.0f) + 60.0f, sy = y * (456.0f / 448.0f) - 70.0f; /* sScreenOffsetX / Y */
            lyt_set_pos(l, sx - 304.0f, 228.0f - sy);
            lyt_update(l);
        }
    }
}
static void subm_draw(void) { if (subm_vis) for (int k = 0; k < 2; k++) if (subm[k].l && subm[k].shown) lyt_draw(subm[k].l); }

/* MarioMeter's HitPointMeter (MeterLayout, Screen/MeterLayout.cpp): the life plate. Wait (with 3 life it slides 50 out
 * to the right while Mario moves, _4C), Danger1 / Danger2 pulsing at rate 4 (1 life) / 2 (2), Damage1 / Damage2 shakes,
 * Recover (Font's Flash, the number's CountUpPaneRumbler), BreakMeter (4..6 down to 3: Break, then FontPosition2to1),
 * ZeroMeter / ZeroMeterBreak (hidden at 0), PowerUp (the life-up mushroom: the W plate by Mario's head counting to 6,
 * MeterMove back to its place, CounterMove: FontPosition1to2, 4 5 6), WaitStart (240 frames); its ShowHide frame
 * _4C x mAlpha x 20, the pointer on PicBase fading it to 0.3. ponytail: no SuddenDeathMeter (a life of 1), no Break /
 * Break123 layout effects */
enum { ML_HIDE, ML_APPEAR, ML_WAIT, ML_WAITSTART, ML_POWERUP, ML_METERMOVE, ML_COUNTERMOVE, ML_RECOVER, ML_DAMAGE,
       ML_BREAKMETER, ML_ZEROMETER, ML_ZEROMETERBREAK };
static struct {
    LytInst *l;
    u8 alive, hidden;
    int count, rum_t;                         /* mCount; the RumbleCalculator's _4 (of 15) */
    float alpha, k;                           /* mAlpha, _4C */
    float pos[2], posw[2], p3c[2], rum[2];    /* mFollowPos, mFollowPosW, _3C, the rumbler's mPosition */
    float head[2];                            /* calcMarioHeadPosition (game.c, each frame) */
    Nrv n;
} hm;
static float hm_ease(float x) { return (1.0f - cosf(x * 3.14159265f)) / 2.0f; } /* getEaseInOutValue(x, 0, 1, 1) */
static void hm_frame_stop(int ch, float f) { lyt_set_frame(hm.l, ch, f); lyt_set_rate(hm.l, ch, 0); } /* setAnimFrameAndStop */
static void hm_count_frame(void) /* setCountAnimFrame */
{
    lyt_set_number_rec(hm.l, "HitPointNumber", hm.count);
    lyt_start_pane(hm.l, "HitPointMeter", "Count", 0);
    lyt_set_pane_frame_stop(hm.l, "HitPointMeter", 0, (float)hm.count);
}
static void hm_recovery_frame(void) /* setRecoveryCountAnimFrame */
{
    lyt_set_number_rec(hm.l, "HitPointNumber", hm.count);
    lyt_start_pane(hm.l, "HitPointMeter", "RecoveryCount", 0);
    lyt_set_pane_frame_stop(hm.l, "HitPointMeter", 0, (float)hm.count);
}
static void hm_anim_base(void) /* setAnimBase */
{
    hm_count_frame();
    lyt_start(hm.l, hm.count <= 3 ? "Danger1" : "Danger2", 1);
    if (hm.count <= 1) lyt_set_rate(hm.l, 1, 4.0f);
    else if (hm.count <= 2) lyt_set_rate(hm.l, 1, 2.0f);
    else hm_frame_stop(1, 0.0f);
}
static void hm_zero_follow(void) { hm.pos[0] = hm.pos[1] = hm.posw[0] = hm.posw[1] = 0; }
void menu_meter_init_count(int life) /* a new MeterLayout (the scene's, or the scene again after a miss) and initCount */
{
    LytInst *l = hm.l;
    memset(&hm, 0, sizeof hm);
    hm.l = l; hm.count = life; hm.k = 1.0f; hm.rum_t = 15; /* mCount(3), mAlpha(), _4C(1) */
    if (!l) return;
    lyt_follow(l, "HPMeter", hm.pos, 1);
    lyt_follow(l, "HitPointW", hm.posw, 1);
    lyt_follow(l, "HitPointNumber", hm.rum, 1); /* CountUpPaneRumbler */
    lyt_start(l, "FontPosition2to1", 1);
    lyt_start(l, "ShowHide", 2);
    hm_frame_stop(2, hm.alpha * hm.k * 20.0f);
    hm_count_frame();
    hm.alive = 1; hm.n.next = -1; nrv_set(&hm.n, ML_WAIT); /* appear */
}
static void hm_init(void) { hm.l = lyt_new("HitPointMeter"); menu_meter_init_count(3); }
void menu_meter_count(int life) /* setLifeCount (setCount) */
{
    int prev = hm.count;
    hm.count = life;
    if (life < prev) nrv_set(&hm.n, life == 0 ? ML_ZEROMETER : prev >= 4 && life < 4 ? ML_BREAKMETER : ML_DAMAGE);
    else if (prev < life) nrv_set(&hm.n, ML_RECOVER);
}
void menu_meter_power_up(void) /* requestPowerUp */
{
    if (!hm.l) return;
    hm.k = 1.0f;
    hm_frame_stop(2, hm.k * 20.0f);
    hm_zero_follow();
    hm.alive = 1;
    nrv_set(&hm.n, ML_POWERUP);
}
void menu_meter_active(int on) /* requestActive / requestDeactivate */
{
    if (!hm.l) return;
    if (!on) { hm.alive = 0; return; }
    if (hm.alive) return;
    hm.rum_t = 15; /* mCountUpPaneRumbler->reset */
    if (hm.count == 3) { hm.k = 0; hm.pos[0] = (1.0f - hm_ease(0.0f)) * 50.0f; } /* getEaseInOutValue(0, 1, 0, 1) x 50 */
    else { hm.k = 1.0f; hm.pos[0] = hm.pos[1] = 0; }
    hm_frame_stop(2, hm.k * 20.0f);
    hm.posw[0] = hm.posw[1] = 0;
    hm.alive = 1;
    nrv_set(&hm.n, ML_WAIT);
}
void menu_meter_moving(int moving) /* requestPlayerMoving / requestPlayerStopped */
{
    if (!nrv_is(&hm.n, ML_WAIT)) return;
    if (hm.count == 3) hm.k = fminf(fmaxf(hm.k + (moving ? -0.05f : 0.05f), 0.0f), 1.0f);
    else hm.k = 1.0f;
}
void menu_meter_head(float x, float y) { hm.head[0] = x; hm.head[1] = y; }
int menu_test_meter(void) { return hm.alive * 1000 + hm.n.cur * 10 + hm.count; } /* tests: alive, nerve, count */
void menu_meter_update(void) /* LayoutActor::movement: the nerve, control, the animations */
{
    if (!hm.l || !hm.alive) return;
    switch (nrv_begin(&hm.n)) {
    case ML_HIDE: break;
    case ML_APPEAR:
        if (FIRST(&hm.n)) { hm.posw[0] = hm.posw[1] = 0; hm_anim_base(); hm.k = 1.0f; }
        hm.pos[0] = hm.pos[1] = 0;
        if (hm.n.step > 120) nrv_set(&hm.n, ML_WAIT); /* sAppearTime */
        break;
    case ML_WAITSTART:
        if (FIRST(&hm.n)) { hm_anim_base(); hm.k = 1.0f; }
        hm_zero_follow();
        if (hm.n.step > 240) nrv_set(&hm.n, ML_WAIT); /* sWaitStartTime */
        break;
    case ML_WAIT:
        if (FIRST(&hm.n)) { hm_anim_base(); hm.pos[0] = hm.pos[1] = 0; }
        hm.pos[0] = (1.0f - hm_ease(hm.k)) * 50.0f; /* getEaseInOutValue(_4C, 1, 0, 1) x 50 */
        hm.posw[0] = hm.posw[1] = 0;
        break;
    case ML_POWERUP: {
        float base[3];
        hm.pos[0] = hm.pos[1] = 0;
        lyt_evaluate(hm.l);
        lyt_pane_place(hm.l, "MoveMeterW", base); /* calcPowerUpMeterBasePosition */
        hm.posw[0] = hm.head[0] - base[0]; hm.posw[1] = hm.head[1] - base[1];
        if (FIRST(&hm.n)) {
            hm.hidden = 0; /* showLayout */
            lyt_start(hm.l, "Danger1", 1); hm_frame_stop(1, 0.0f);
            hm_recovery_frame();
            hm.k = 1.0f;
        }
        if (hm.count < 6 && hm.n.step > 0 && hm.n.step % 5 == 0) { /* sRecoveryCountInterval */
            hm.count++;
            hm_recovery_frame();
            if (hm.count >= 4) lyt_set_number_rec(hm.l, "HitPointNumber", 3);
            else { hm.rum_t = 0; lyt_start_pane(hm.l, "Font", "Flash", 0); }
        }
        if (hm.n.step > 60) nrv_set(&hm.n, ML_METERMOVE); /* sPowerUpWaitTime */
        break;
    }
    case ML_METERMOVE: {
        if (FIRST(&hm.n)) { hm.p3c[0] = hm.posw[0]; hm.p3c[1] = hm.posw[1]; hm.k = 1.0f; }
        float r = fminf((float)hm.n.step / 25.0f, 1.0f); /* calcNerveEaseInRate(sMoveToBasePositionTime) */
        r = 1.0f - cosf(r * 3.14159265f / 2.0f);
        hm.posw[0] = hm.p3c[0] * (1.0f - r); hm.posw[1] = hm.p3c[1] * (1.0f - r);
        if (hm.n.step > 25) nrv_set(&hm.n, ML_COUNTERMOVE);
        break;
    }
    case ML_COUNTERMOVE:
        if (FIRST(&hm.n)) { lyt_start(hm.l, "FontPosition1to2", 1); hm.k = 1.0f; }
        if (hm.n.step == 0) lyt_set_number_rec(hm.l, "HitPointNumber", 4);
        if (hm.n.step == 5) lyt_set_number_rec(hm.l, "HitPointNumber", 5);
        if (hm.n.step == 10) lyt_set_number_rec(hm.l, "HitPointNumber", 6);
        if (lyt_stopped(hm.l, 1)) nrv_set(&hm.n, ML_WAITSTART);
        break;
    case ML_RECOVER:
        if (FIRST(&hm.n)) {
            hm.rum_t = 0; lyt_start_pane(hm.l, "Font", "Flash", 0);
            hm_count_frame(); hm_zero_follow(); hm.k = 1.0f;
        }
        if (lyt_pane_stopped(hm.l, "Font", 0)) nrv_set(&hm.n, ML_WAITSTART);
        break;
    case ML_DAMAGE:
        if (FIRST(&hm.n)) {
            lyt_start(hm.l, hm.count < 3 ? "Damage1" : "Damage2", 1);
            hm_zero_follow(); hm_count_frame(); hm.k = 1.0f;
        }
        if (lyt_stopped(hm.l, 1)) nrv_set(&hm.n, ML_WAITSTART);
        break;
    case ML_BREAKMETER:
        if (FIRST(&hm.n)) {
            lyt_start_pane(hm.l, "HitPointMeter", "Break", 0);
            lyt_set_number_rec(hm.l, "HitPointNumber", hm.count);
            lyt_start(hm.l, "Damage2", 1);
            hm_zero_follow(); hm.k = 1.0f;
        }
        if (hm.n.step == 40) { /* sBreakDelayTime (emitEffect Break) */
            se_start(SE_SY_LIFE_PLATE_DEC, NULL);
            lyt_start(hm.l, "FontPosition2to1", 1);
            hm_count_frame();
            nrv_set(&hm.n, ML_WAITSTART);
        }
        break;
    case ML_ZEROMETER:
        if (FIRST(&hm.n)) { lyt_start(hm.l, "Damage1", 1); hm_zero_follow(); hm_count_frame(); hm.k = 1.0f; }
        if (lyt_stopped(hm.l, 1)) nrv_set(&hm.n, ML_ZEROMETERBREAK);
        break;
    case ML_ZEROMETERBREAK:
        if (FIRST(&hm.n)) hm_zero_follow();
        if (hm.n.step == 40) hm.hidden = 1; /* (emitEffect Break123) hideLayout */
        break;
    }
    nrv_end(&hm.n);
    /* control: mAlpha (the pointer on PicBase), the ShowHide frame, the rumbler */
    float cx, cy;
    if (nrv_is(&hm.n, ML_APPEAR)) hm.alpha = 1.0f;
    else if (game_cursor(&cx, &cy) && lyt_pane_hit(hm.l, "PicBase", cx, cy)) hm.alpha -= 0.05f;
    else hm.alpha += 0.05f;
    hm.alpha = fminf(fmaxf(hm.alpha, 0.3f), 1.2f); /* sMinAlpha, sMaxAlpha */
    hm_frame_stop(2, hm.k * fminf(hm.alpha, 1.0f) * 20.0f); /* normalize(mAlpha, 0, 1) */
    hm.rum[0] = hm.rum[1] = 0;
    if (hm.rum_t < 15) { /* RumbleCalculatorCosMultLinear(cRumbleFreq 2, pi / 2, cRumbleAmp 8, cRumbleFrame 15): its y */
        float pr = hm.rum_t / 15.0f;
        hm.rum[1] = cosf(2.0f * pr * 6.2831853f + 1.5707963f) * (1.0f - pr) * 8.0f;
        hm.rum_t++;
    }
    lyt_update(hm.l);
}
static void hm_draw(void) { if (hm.l && hm.alive && !hm.hidden) lyt_draw(hm.l); }

int menu_update(void)
{
    if (select_after[0]) { menu_scenario_select(select_after, 0); select_after[0] = 0; }
    fade_update();
    wipe_update();
    dmu_update();
    mlay_update();
    mono_update();
    llt_update();
    srl_update();
    spg_update();
    if (sr.delay && --sr.delay == 0 && sr.l) { /* the clear's demo sequence: StageResultInformer, then the save */
        sr.alive = 1; sr.n.next = -1; nrv_set(&sr.n, SR_APPEARSTAR);
    }
    if (sr.alive && !menu_galaxy_map_active()) { /* (paused while the map it opened shows) */
        sr_update();
        return !sr.demo; /* a return demo's (requestMovementOn): the scene keeps moving under it, Mario in ResultWait */
    }
    sv_take_errors();
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
 * (calcWorldPositionFromScreenPos, depth 1000); a comet's scenario decided, its warning first. ponytail: no
 * ScenarioSelectEffect / star effects, no BGM, the star's colour (BTP: all
 * yellow); no race best times (BestTime stays out); isPlacedCoin taken as true; hidden stars: no Luigi / green star
 * checks */
enum { SS_APPEARSTAR, SS_APPEAR, SS_WAIT, SS_DECIDE, SS_AFTER, SS_CANCEL, SS_CANCELFADE, SS_COMETAPPEAR, SS_COMETWAIT, SS_COMETEND };
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
    int decided;                        /* isScenarioDecided: its scenario (a comet's), 0 the choice */
    u16 comet_txt[96];
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
    if (ss.decided) { *normal = 1; *extra = 0; return !ss_has(ss.decided); }
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
    if (ss.decided) ss_appear_star(0, ss.decided, is_new ? 0 : 2);
    for (int i = 0; i < normal && !ss.decided; i++) ss_appear_star(i, i + 1, !is_new ? 2 : i < normal - 1 ? 1 : 0);
    int nn = ss_normal_num(), power = gd_power_star_num(ss.galaxy), idx = 3;
    for (int j = 0; j < power - nn && !ss.decided; j++)
        if (ss_has(nn + j + 1)) ss_appear_star(idx++, nn + j + 1, is_new ? 1 : 2);
    lyt_show_rec(ss.l, "New", 0);
    lyt_show_rec(ss.l, "NewStarGreen", 0);
    for (int i = 0; i < 2; i++) lyt_show_rec(ss.l, ss_question_pane[i], 0);
    if (is_new) {
        lyt_show_rec(ss.l, "New", 1);
        lyt_copy_pane_trans(ss.l, ss_star_pane[normal - 1], ss.new_pos);
    } else if (!ss.decided) {
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
        if (all) nrv_set(&ss.n, ss.decided && gd_scenario_comet(ss.galaxy, ss.decided)[0] ? SS_COMETAPPEAR : SS_APPEAR);
        break;
    }
    case SS_COMETAPPEAR: /* exeAppearCometWarning: "GalaxyInfo_CometAppearR" with its comet's name, at 15 */
        if (ss.n.step == 15) {
            char id[40];
            snprintf(id, sizeof id, "CometName_%s", gd_scenario_comet(ss.galaxy, ss.decided));
            const u16 *args[1] = {msg_get(id)};
            msg_with_args(ss.comet_txt, 96, msg_get("GalaxyInfo_CometAppearR"), args, 1);
            lyt_set_text_rec(ss.l, "TextComet", ss.comet_txt);
            lyt_show_rec(ss.l, "CometAppear", 1);
            lyt_start_pane(ss.l, "CometAppear", "CometAppear", 0);
            se_start(SE_SY_COMET_WARNING_DISP, NULL);
        }
        if (ss.n.step > 15 && lyt_pane_stopped(ss.l, "CometAppear", 0)) nrv_set(&ss.n, SS_COMETWAIT);
        break;
    case SS_COMETWAIT:
        if (ss.n.step % 80 == 25) se_start(SE_SY_COMET_WARNING, NULL);
        if (FIRST(&ss.n)) lyt_start_pane(ss.l, "CometAppear", "CometWait", 0);
        if (ss.n.step == 180) nrv_set(&ss.n, SS_COMETEND); /* cCometWarningFrame */
        break;
    case SS_COMETEND:
        if (FIRST(&ss.n)) lyt_start_pane(ss.l, "CometAppear", "CometEnd", 0);
        if (ss.n.step > 0 && lyt_pane_stopped(ss.l, "CometAppear", 0)) nrv_set(&ss.n, SS_APPEAR);
        break;
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

void menu_scenario_select(const char *galaxy, int decided)
{
    if (!ss.l || !ss.m_star || !ss.anim) { /* no layout: straight in at its first scenario */
        seq_galaxy_move(SEQ_MOVE, galaxy, 1, -1, 0);
        return;
    }
    fade.frames = 0;
    snprintf(ss.galaxy, sizeof ss.galaxy, "%s", galaxy);
    ss.decided = decided;
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
    lyt_show_rec(ss.l, "CometAppear", 0); /* RMGE01: hidden until its warning */
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
    if (menu_staffroll_on()) { srl_draw(); return 1; } /* StaffRoll::appear: deactivateGameSceneDraw3D */
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
    { /* showCometWindow (a comet landed on an open galaxy: GalaxyInfo_CometAppear with its name) / hideCometWindow */
        static u16 ctext[96];
        int cs = state == 3 ? gd_comet_star(g) : 0;
        lyt_start(l, "CometWindow", 1);
        lyt_set_frame(l, 1, cs ? 1.0f : 0.0f); lyt_set_rate(l, 1, 0);
        if (cs) {
            char id[40];
            snprintf(id, sizeof id, "CometName_%s", gd_scenario_comet(g, cs));
            const u16 *args[1] = {msg_get(id)};
            msg_with_args(ctext, 96, msg_get("GalaxyInfo_CometAppear"), args, 1);
        } else ctext[0] = 0;
        lyt_set_text_rec(l, "CometName", ctext);
    }
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
 * right stick, X decides, Circle / Triangle is B. ponytail: fellows only (no Miis), no Manual button, no copy source marker (FileSelectEffect), no blink / morph blur / effects / ME / BGM, no swipe
 * spin, the save's date not shown */
enum { FS_OFF, FS_TITLE, FS_TITLEEND, FS_SELECTSTART, FS_SELECT, FS_CONFIRMSTART, FS_CONFIRM, FS_DEMOSTARTWAIT,
       FS_CREATECONFIRMSTART, FS_CREATECONFIRM, FS_CREATE, FS_DELETECONFIRMSTART, FS_DELETECONFIRM, FS_DELETE,
       FS_COPYWAIT, FS_COPYSELECT, FS_COPYCONFIRMSTART, FS_COPYCONFIRM, FS_COPYDEMO,
       FS_MIIWAIT, FS_MIISELECT, FS_MIICONFIRMWAIT, FS_MIICONFIRM, FS_MIIDEMO };
enum { TS_LOGOFADEIN, TS_LOGOWAIT, TS_LOGODISPLAY, TS_DECIDE, TS_DEAD };
enum { FC_TITLE, FC_MOVEFAR, FC_FAR, FC_MOVENEAR, FC_NEAR };
enum { FI_NEW, FI_EXIST, FI_FORMAT, FI_CHANGE };
enum { FB_START, FB_DELETE, FB_COPY, FB_MII, FB_N };
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


static void fs_info_set(int slot);
static int fsi_luigi; /* the info's file plays as Luigi (FileSelectInfo mIsSelectedMario) */
/* BrosButton ("ルイージ切り替えボタン": ButtonPaneController BrosButton / BoxBButton) in the file confirm while
 * isUserFileAppearLuigi; a decide flips the player (SE_SY_FILE_SEL_SW_MARIO / LUIGI): setUserFileMario, the file's info
 * again, FileSelectInfo::change (CharaState: MariottoLuigi / LuigitoMario on channel 2, reflectInfo at their step 10) */
static struct { LytInst *l; Btn b; u8 on, mario; int chara, chara_step; } bros; /* chara: 0 Mario, 1 ToLuigi, 2 Luigi, 3 ToMario */
static void bros_init(void)
{
    memset(&bros, 0, sizeof bros);
    if (!(bros.l = lyt_new("BrosButton"))) return;
    btn_init(&bros.b, bros.l, "BrosButton", "BoxBButton", 0, 1);
    bros.b.end_disappear = 0;
}
static void bros_appear(int mario) { if (bros.l) { bros.on = 1; bros.mario = (u8)mario; btn_appear(&bros.b); } }
static void bros_disappear(void) { if (bros.on && !btn_hidden(&bros.b)) btn_disappear(&bros.b); }
static int bros_hidden(void) { return !bros.on || btn_hidden(&bros.b); }
static void bros_update(void) /* BrosButton::control and exeSelect */
{
    if (!bros.on) return;
    if (btn_pointing_trigger(&bros.b)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
    if ((btn_is(&bros.b, BP_WAIT) || btn_is(&bros.b, BP_POINTING) || btn_is(&bros.b, BP_NOTPOINTING)) && btn_try_select(&bros.b)) {
        bros.mario = !bros.mario;
        se_start(bros.mario ? SE_SY_FILE_SEL_SW_MARIO : SE_SY_FILE_SEL_SW_LUIGI, NULL);
    }
    btn_update(&bros.b);
    lyt_update(bros.l);
    if (btn_hidden(&bros.b) && !btn_appearing(&bros.b)) bros.on = 0;
}
static int bros_selected(void) { return bros.on && btn_is(&bros.b, BP_DECIDEDWAIT); } /* isSelected (the Decide nerve) */
static void bros_draw(void) { if (bros.on) lyt_draw(bros.l); }
static void fs_chara(int luigi, int change) /* FileSelectInfo::change (or forceChange: its state at once) */
{
    static const char *const anim[4] = {"MarioWait", "MariottoLuigi", "LuigiWait", "LuigitoMario"};
    int to = luigi ? (change ? 1 : 2) : (change ? 3 : 0);
    if (change && (luigi ? bros.chara == 1 || bros.chara == 2 : bros.chara == 3 || bros.chara == 0)) return; /* there or going */
    bros.chara = to; bros.chara_step = 0;
    if (fsl.info) lyt_start(fsl.info, anim[to], 2);
}
static void fs_chara_update(void) /* CharaState: ToLuigi / ToMario end in Luigi / Mario */
{
    if (!fsl.info || (bros.chara != 1 && bros.chara != 3)) return;
    int slot = fsl.sel >= 0 ? fsl.sel : fsl.hover;
    if (++bros.chara_step == 10 && slot >= 0) fs_info_set(slot); /* reflectInfo */
    if (lyt_stopped(fsl.info, 2)) fs_chara(bros.chara == 1, 0);
}

/* MiiSelect with its fellows only (FileSelectIconID 0 Mario, 1 Luigi, 2 Yoshi, 3 Kinopio, 4 Peach; Luigi once
 * isUserFileAppearLuigi), one page (Mii01..08 while it appears, Mii09..16 after: setCurrentPageGroupA / B), each a
 * MiiIcon at its "Character" frame (sCharacterTexFrame); X on one selects it. ponytail: no Miis, no page scroll,
 * no MiiConfirmIcon (the window alone) */
static const float mii_frame[5] = {0, 4, 3, 2, 1};
static struct { LytInst *l, *icon[5]; Btn b[5]; int fellow[5], n, on, selected, first, prohibit, nrv_end, waiting; Nrv n2; } mii;

static void mii_appear(int first, int prohibit)
{
    if (!mii.l) return;
    mii.first = first; mii.prohibit = prohibit; mii.selected = -1; mii.n = 0;
    int luigi = fsl.sel >= 0 && gd_file_appear_luigi(fsl.item[fsl.sel].file - 1); /* isUserFileAppearLuigi */
    for (int f = 0; f < 5; f++) if (f != 1 || luigi) mii.fellow[mii.n++] = f;
    for (int i = 0; i < 5; i++) {
        if (!mii.icon[i]) continue;
        lyt_start(mii.icon[i], "Character", 0);
        if (i < mii.n) { lyt_set_frame(mii.icon[i], 0, mii_frame[mii.fellow[i]]); lyt_start_pane(mii.icon[i], "MarioIcon", "Character", 1); lyt_set_pane_frame_stop(mii.icon[i], "MarioIcon", 1, mii_frame[mii.fellow[i]]); }
        lyt_set_rate(mii.icon[i], 0, 0);
        lyt_show_pane(mii.icon[i], "ShaMii", 0); lyt_show_pane(mii.icon[i], "PicMii", 0); lyt_show_pane(mii.icon[i], "PicMiiShine", 0);
        btn_appear(&mii.b[i]);
        lyt_set_alpha(mii.icon[i], i < mii.n && mii.fellow[i] + 1 == prohibit ? 0.4f : 1.0f); /* prohibitIcon */
    }
    lyt_start(mii.l, "Appear", 0);
    lyt_set_text_rec(mii.l, "TxtName", NULL);
    lyt_show_rec(mii.l, "Arrow", 0); lyt_show_rec(mii.l, "PageNumber", 0);
    se_start(SE_SY_FILE_SEL_MIISEL_OPEN, NULL);
    mii.on = 1; mii.nrv_end = 0; mii.waiting = 0;
}
static void mii_update(void)
{
    if (!mii.on) return;
    if (!mii.waiting && !mii.nrv_end && lyt_stopped(mii.l, 0)) { mii.waiting = 1; lyt_start(mii.l, "Wait", 0); } /* exeWait */
    int appearing = !mii.waiting;
    for (int i = 0; i < mii.n; i++) {
        char pane[8];
        float p[3];
        snprintf(pane, sizeof pane, "Mii%02d", i + (appearing ? 1 : 9));
        lyt_evaluate(mii.l);
        lyt_pane_place(mii.l, pane, p);
        lyt_set_pos(mii.icon[i], p[0] - 304.0f, 228.0f - p[1]);
        if (!mii.nrv_end && !appearing && mii.selected < 0 && mii.fellow[i] + 1 != mii.prohibit) {
            if (btn_pointing_trigger(&mii.b[i])) {
                char label[40];
                snprintf(label, sizeof label, "System_FileSelect_Icon%03d", mii.fellow[i]);
                lyt_set_text_rec(mii.l, "TxtName", msg_get(label));
                se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
            }
            if (btn_try_select(&mii.b[i])) { mii.selected = mii.fellow[i]; se_start(SE_SY_FILE_SEL_MII_SELECTED, NULL); }
        }
        btn_update(&mii.b[i]);
        lyt_update(mii.icon[i]);
    }
    lyt_update(mii.l);
    if (mii.nrv_end && lyt_stopped(mii.l, 0)) mii.on = 0;
}
static void mii_disappear(void) { if (mii.on && !mii.nrv_end) { lyt_start(mii.l, "End", 0); mii.nrv_end = 1; } }
static void mii_draw(void)
{
    if (!mii.on) return;
    lyt_draw(mii.l);
    for (int i = 0; i < mii.n; i++) lyt_draw(mii.icon[i]);
}
static void mii_init(void)
{
    memset(&mii, 0, sizeof mii);
    if (!(mii.l = lyt_new("MiiSelect"))) return;
    for (int i = 0; i < 5; i++) {
        if (!(mii.icon[i] = lyt_new("MiiIcon"))) { mii.l = NULL; return; }
        btn_init(&mii.b[i], mii.icon[i], "MarioIcon", "PicMario", 0, 0);
    }
}

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
    /* playPointedNotUsingME in NewWait (a file not made yet), else playPointedME */
    me_start_random(gd_file_created(it->file - 1) ? "ME_ASTRO_DOME_HIT_GALAXY" : "ME_ASTRO_DOME_HIT_GALAXY_N", 5);
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
    fsi_luigi = gd_file_luigi(it->file - 1); /* reflectInfo: BrosIcon when Luigi's or after the complete ending */
    lyt_show_rec(fsl.info, "BrosIcon", fsi_luigi || ce);
    lyt_show_pane(fsl.info, "TxtMario", !fsi_luigi);
    lyt_show_pane(fsl.info, "TxtLuigi", fsi_luigi);
}
static void fs_info_appear(void)
{
    if (!fsl.info) return;
    if (!fsl.info_on) { lyt_start(fsl.info, "Appear", 0); fs_chara(fsi_luigi, 0); lyt_start(fsl.info, "ButtonAppear", 1); lyt_set_frame(fsl.info, 1, 0); lyt_set_rate(fsl.info, 1, 0); }
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
    bros_disappear();
    fs_info_disappear();
    if (fsl.back_on && !fsl.back_clicked) btn_disappear(&fsl.back);
}
static int fs_layouts_hidden(void)
{
    for (int b = 0; b < FB_N; b++) if (fsl.op_on && !btn_hidden(&fsl.btn[b])) return 0;
    return !fsl.info_on && (!fsl.back_on || btn_hidden(&fsl.back)) && bros_hidden();
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
            me_start_random("ME_ASTRO_DOME_SELECT", 4); /* FileSelector::onSelect: playSelectedME */
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
            btn_appear(&fsl.btn[FB_START]); btn_appear(&fsl.btn[FB_DELETE]); btn_appear(&fsl.btn[FB_COPY]); btn_appear(&fsl.btn[FB_MII]); fsl.op_on = 1;
            btn_appear(&fsl.back); fsl.back_on = 1; fsl.back_clicked = 0;
            if (!fsl.info_on) { fs_info_set(fsl.sel); fs_info_appear(); }
            lyt_start(fsl.info, "ButtonAppear", 1); /* slide */
            if (gd_file_appear_luigi(fsl.item[fsl.sel].file - 1)) bros_appear(!gd_file_luigi(fsl.item[fsl.sel].file - 1));
        }
        if (bros_selected()) { /* setUserFileMario, restoreUserFile, setFileInfo, change, resume */
            gd_file_set_luigi(fsl.item[fsl.sel].file - 1, !bros.mario);
            fs_chara(!bros.mario, 1);
            btn_force_wait(&bros.b);
        }
        if (fs_back_selected()) { fs_layouts_out(); fs_clear_pointing(); nrv_set(&fsl.n, FS_SELECTSTART); break; }
        for (int b = 0; b < FB_N; b++) if (btn_pointing_trigger(&fsl.btn[b])) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
        if (btn_try_select(&fsl.btn[FB_START])) nrv_set(&fsl.n, FS_DEMOSTARTWAIT);
        else if (btn_try_select(&fsl.btn[FB_DELETE])) nrv_set(&fsl.n, FS_DELETECONFIRMSTART);
        else if (btn_try_select(&fsl.btn[FB_COPY])) nrv_set(&fsl.n, FS_COPYWAIT);
        else if (btn_try_select(&fsl.btn[FB_MII])) { fs_layouts_out(); fs_items_select(0); nrv_set(&fsl.n, FS_MIIWAIT); }
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
    case FS_CREATE: /* Create (the file saved), then its icon chosen: MiiSelectStartFirst */
        if (FIRST(&fsl.n)) { gd_file_create(fsl.item[fsl.sel].file - 1, 1); menu_save_request(); mii_appear(1, 0); }
        nrv_set(&fsl.n, FS_MIISELECT);
        break;
    case FS_MIIWAIT:
        if (FIRST(&fsl.n)) se_start(SE_SY_FILE_SEL_UPPER_DECIDE, NULL);
        if (fs_layouts_hidden()) { mii_appear(0, gd_file_icon(fsl.item[fsl.sel].file - 1)); nrv_set(&fsl.n, FS_MIISELECT); }
        break;
    case FS_MIISELECT:
        if (FIRST(&fsl.n) && !mii.first) { btn_appear(&fsl.back); fsl.back_on = 1; fsl.back_clicked = 0; }
        if (mii.selected >= 0 && fsl.n.step > 0) { mii_disappear(); if (fsl.back_on) btn_disappear(&fsl.back); nrv_set(&fsl.n, FS_MIICONFIRMWAIT); }
        else if (!mii.first && fs_back_selected()) { mii_disappear(); nrv_set(&fsl.n, FS_CONFIRM); } /* MiiCancel */
        break;
    case FS_MIICONFIRMWAIT:
        if (!mii.on) nrv_set(&fsl.n, FS_MIICONFIRM);
        break;
    case FS_MIICONFIRM: /* "this icon?" (013 the first time, 005 a change) */
        if (FIRST(&fsl.n)) si_appear(&fsl.win, mii.first ? "System_FileSelect013" : "System_FileSelect005", SI_YESNO);
        if (!fsl.win.alive && fsl.n.step > 0) {
            if (yn_yes(&fsl.win.yn)) { /* MiiCreateWait: store the icon, save all */
                se_start(SE_SY_FILE_SEL_MII_CHANGE, NULL);
                gd_file_set_icon(fsl.item[fsl.sel].file - 1, mii.selected + 1);
                menu_save_request();
                nrv_set(&fsl.n, FS_MIIDEMO);
            } else { mii_appear(mii.first, mii.prohibit); nrv_set(&fsl.n, FS_MIISELECT); }
        }
        break;
    case FS_MIIDEMO: /* MiiCreateDemo: the change morph */
        if (FIRST(&fsl.n)) { FsItem *it = &fsl.item[fsl.sel]; it->icon = mii.selected + 1; it->n.next = -1; nrv_set(&it->n, FI_CHANGE); }
        if (nrv_is(&fsl.item[fsl.sel].n, FI_EXIST) && fsl.n.step > 0) { fs_info_set(fsl.sel); nrv_set(&fsl.n, FS_CONFIRM); }
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
            me_start_random("ME_ASTRO_DOME_SELECT", 4); /* FileSelector::onSelect: playSelectedME */
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
    bros_update();
    fs_chara_update();
    mii_update();
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
    btn_init(&fsl.btn[FB_MII], fsl.op, "MiiButton", "PicMii", 0, 1);
    mii_init(); bros_init(); lyt_show_pane(fsl.op, "P2ManualButton", 0);
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
    } else if (!lyt_pane_center(fsl.win.alive ? fsl.win.l : mii.on ? mii.l : fsl.op, pane, &x, &y)) return 0;
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
    bros_draw();
    if (fsl.back_on) lyt_draw(fsl.back_l);
    if (fsl.logo_on) lyt_draw(fsl.logo);
    if (fsl.press_on) lyt_draw(fsl.press);
    mii_draw();
    if (fsl.win.alive) lyt_draw(fsl.win.l);
    if (sv.win.alive) lyt_draw(sv.win.l); /* SaveDataHandleSequence's (the boot's card windows) */
    wipe_draw_quad();
    fade_draw();
    /* the camera's target and fovy a frame late (setProgrammableCameraParam with the previous ones) */
    memcpy(fsl.ptgt, fsl.tgt, 12); fsl.pfovy = fsl.fovy;
}

/* --- GalaxyMapController, GalaxyMap, its icons and PowerStarList after Petari (map.bin: the Observatory's), opened in
 * its modes by ButlerMap's talk (startGalaxyMapLayout / startAstroMapLayout), its lecture (ForNewDomeDiscover), the
 * Butler's demo (ForChallengeGalaxyDiscover) and the stage result (ForNewGalaxyDiscover / ForNewTicoGalaxyDiscover);
 * the world frozen while it shows. The pointer is the right stick, X is A (an icon: its detail; empty space: the other
 * view), Circle / Triangle is B. Its Mario icons (GalaxyMapMarioIcon: cMarioIconPositions, in a dome also
 * cMarioIconPositionsInDome), Tico icons (GalaxyMapTicoIcon: the hungry Lumas' galaxies, their meter) and comet icons
 * (GalaxyMapCometIcon: a galaxy with comets where one landed, its colour by the comet) follow their map panes.
 * GalaxyDetail's picture (GalaxyInfoTexture) is read from mappics.bin into one texture when the detail opens.
 * ponytail: no AstroMapBoard capture, no dome name plates (GalaxyMapDomeIcon's GalaxyNamePlate), no IconAButton;
 * startGalaxyMapLayoutForTicoComet has no caller (TicoComet's demo is not ported); the star list without its race
 * best times (no race times in the save) or its capture (WiiConnect24) */
enum { GM_OFF, GM_READYMODEMAP, GM_FADEINMODEMAP, GM_APPEARGALAXY, GM_DISPLAYGALAXY, GM_APPEARASTRO, GM_DISPLAYASTRO,
       GM_READYLIST, GM_APPEARLIST, GM_DISPLAYLIST, GM_DISAPPEARLIST, GM_NGD_ASTRO, GM_NGD_APPEARGALAXY,
       GM_NGD_DISPLAYGALAXY, GM_NTGD_ASTRO, GM_NDD_ASTRO, GM_TC_FADEOUTWHITE, GM_TC_WAITWHITE, GM_TC_PRAY, GM_CGD_ASTRO,
       GM_FADEOUT, GM_READYGAME };
enum { MV_IDLE, MV_FOCUSIN, MV_FOCUSOUT, MV_FADEINGALAXY, MV_FADEINASTRO, MV_DETAIL };
enum { MT_HIDE, MT_DISPLAY, MT_APPEARTOANOTHER, MT_DISAPPEARTOANOTHER };
enum { PL_OFF, PL_APPEAR, PL_WAIT, PL_PAGENEXT, PL_PAGEPREV };
#define GM_ICONS 56
typedef struct { LytInst *l; char gal[32], pane[16]; int status; float l_scale; Btn b; } GmIcon;
static struct {
    Nrv n, mv, tn, pl;
    LytInst *map, *bg, *title, *btn_l, *back_l, *detail, *plate, *list, *dome[6];
    GmIcon icon[GM_ICONS];
    int nicon, mode, pointing, detail_on, list_on, page, npages, plate_show;
    int focus_on, icon_mode, tico_mode, dome_mode, guidance, back_on, btn_on, title_on, wipe_frames;
    u32 wipe_rgb, lv;
    const char *title_msg;
    Btn button, back, up, down;
    u8 back_clicked;
    u16 plate_txt[64], page_txt[48];
    const char *list_gal[75]; int nlist;
    float belt[4][2];                 /* PowerStarList's separators' follow offsets (setFollowTypeAdd) */
    LytInst *mario[2], *tico[8], *comet[GM_ICONS]; /* GalaxyMapMarioIcon 1 / 2, GalaxyMapTicoIcon, GalaxyMapCometIcon */
    char mario_pane[2][8], tico_pane[8][8];
    int ntico, tico_on[8], comet_on[GM_ICONS];
    u16 col[5][8][128];               /* the star list's columns: stage 8 panes, star / coin / comp / time 4 */
} gm;

/* GalaxyDetail's picture: mappics.bin (tools/lyt.py build_mappics) read into one buffer, PicDummy's own texture
 * retargeted to it (its material's texture in map.bin; render_set_texture) */
int packs_read(const char *path, u32 off, void *dst, u32 n); /* main.c */
static struct { u8 head[4096] __attribute__((aligned(64))); u8 *buf; int n, tex; u32 slot; TexEntry e; char cur[24]; } gmp;

static void gm_pic_init(void)
{
    gmp.n = 0; gmp.cur[0] = 0;
    gmp.tex = lyt_material_texture(gm.detail, "PicDummy");
    if (gmp.tex < 0 || !packs_read("mappics.bin", 0, gmp.head, sizeof gmp.head)) return;
    gmp.n = (int)*(const u32 *)gmp.head;
    gmp.slot = *(const u32 *)(gmp.head + 4);
    if (gmp.n <= 0 || gmp.n > (int)((sizeof gmp.head - 16) / 56) || !gmp.slot || gmp.slot > (64u << 10)) { gmp.n = 0; return; }
    gmp.buf = (u8 *)(((u32)mem_alloc(MEM_SCENE, gmp.slot + 64) + 63) & ~63u);
}
static void gm_pic(const char *name) /* GalaxyInfoLayoutSetter's replaceTexture: "<galaxy>.bti", cut to 20 */
{
    if (!gmp.n || !strncasecmp(gmp.cur, name, 20)) return;
    for (int i = 0; i < gmp.n; i++) {
        const u8 *r = gmp.head + 16 + 56 * i;
        if (strncasecmp((const char *)r, name, 20)) continue;
        if (!packs_read("mappics.bin", *(const u32 *)(r + 20), gmp.buf, gmp.slot)) return;
        memcpy(&gmp.e, r + 24, sizeof gmp.e);
        render_set_texture((u32)gmp.tex, &gmp.e, gmp.buf);
        snprintf(gmp.cur, sizeof gmp.cur, "%.20s", name);
        return;
    }
}

static int gm_status(const char *g) /* GalaxyMapIcon::examineIconStatus: 0 closed, 1 new, 2 opened, 3 completed */
{
    if (!gd_galaxy_appear(g)) return 0;
    int n = gd_power_star_num(g);
    if (n > 0 && gd_star_num_galaxy(g) == n) return 3;
    if (n == 0 || gd_flag_is_on(g)) return 2;
    return gd_flag_can_on(g) ? 1 : 0;
}
static int gm_new_on_discover(const GmIcon *ic) { return gm.icon_mode && gd_flag_can_on_just(ic->gal); } /* isNewOnDiscoverMode */

static void gm_sync_icons(void)
{
    for (int i = 0; i < gm.nicon; i++) { /* GalaxyMapIcon::syncStatus */
        GmIcon *ic = &gm.icon[i];
        ic->status = gm_status(ic->gal);
        if (!ic->status) continue;
        lyt_start(ic->l, "Status", 0);
        lyt_set_frame(ic->l, 0, (float)ic->status); lyt_set_rate(ic->l, 0, 0);
        int blink = gm.icon_mode ? gd_flag_can_on_just(ic->gal) : ic->status == 1; /* isBlink */
        lyt_start(ic->l, blink ? "NewBlink" : "NewWait", 1);
        if (gm_new_on_discover(ic)) { /* ButtonSelectIn at its end */
            lyt_start_pane(ic->l, "GalaxyIcon", "ButtonSelectIn", 0);
            lyt_set_pane_frame_stop(ic->l, "GalaxyIcon", 0, lyt_anim_frames(ic->l, "ButtonSelectIn"));
        }
    }
    for (int d = 0; d < 6; d++) { /* GalaxyMapDomeIcon::syncStatus (calcDomeStatus) */
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
        if (gm.dome_mode && d + 1 == 2) { /* setModeNewDomeDiscover: the second dome's SelectIn at its end, Blink */
            lyt_start(gm.dome[d], "SelectIn", 1);
            lyt_set_frame(gm.dome[d], 1, lyt_anim_frames(gm.dome[d], "SelectIn")); lyt_set_rate(gm.dome[d], 1, 0);
            lyt_start(gm.dome[d], "Blink", 0);
        } else lyt_start(gm.dome[d], "Wait", 1);
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

static int gm_comet_id(const char *name) /* MR::getCometNameIdFromString */
{
    return !strcmp(name, "Dark") ? 2 : !strcmp(name, "Ghost") ? 1 : !strcmp(name, "Quick") ? 3 :
           !strcmp(name, "Purple") || !strcmp(name, "Black") ? 4 : 0;
}
static void gm_sync_extra(void) /* the icons' appear: GalaxyMapTicoIcon / GalaxyMapCometIcon::syncStatus, showBlink */
{
    for (int k = 0; k < gm.ntico; k++) {
        const char *g;
        int given, max;
        gm.tico_on[k] = 0;
        if (!gm.tico[k] || !gd_tico_galaxy(k, &g, &given, &max) || max <= 0) continue;
        float rate = (float)given / max;
        if (!gd_galaxy_appear(g) || rate >= 1.0f) continue;
        gm.tico_on[k] = 1;
        lyt_start(gm.tico[k], "TicoMeter", 2); lyt_set_frame(gm.tico[k], 2, rate * lyt_anim_frames(gm.tico[k], "TicoMeter")); lyt_set_rate(gm.tico[k], 2, 0);
        if (gm.tico_mode && gd_galaxy_appear_just(g)) { /* isModeDiscoverAndIsAppear: SelectIn at its end, Blink */
            lyt_start(gm.tico[k], "SelectIn", 0);
            lyt_set_frame(gm.tico[k], 0, lyt_anim_frames(gm.tico[k], "SelectIn")); lyt_set_rate(gm.tico[k], 0, 0);
            lyt_start(gm.tico[k], "Blink", 1);
        } else {
            lyt_start(gm.tico[k], "Wait", 0);
            lyt_start(gm.tico[k], "Blink", 1); lyt_set_frame(gm.tico[k], 1, 0); lyt_set_rate(gm.tico[k], 1, 0);
        }
    }
    for (int i = 0; i < gm.nicon; i++) {
        gm.comet_on[i] = 0;
        int cs = gm.comet[i] ? gd_comet_star(gm.icon[i].gal) : 0; /* isGalaxyCometLandInStage */
        if (!cs) continue;
        gm.comet_on[i] = 1;
        lyt_start(gm.comet[i], "Color", 1); /* setCometAnimFromId */
        lyt_set_frame(gm.comet[i], 1, (float)gm_comet_id(gd_scenario_comet(gm.icon[i].gal, cs))); lyt_set_rate(gm.comet[i], 1, 0);
    }
    for (int k = 0; k < 2; k++) if (gm.mario[k]) lyt_start(gm.mario[k], "Wait", 0);
}

/* GalaxyMapTitle: TitleEnd, the message changed, TitleAppear, TitleWait; Guidance hidden but in the normal mode */
static void gm_title_start(const char *msg, int another) { gm.title_on = 1; gm.title_msg = msg; nrv_set(&gm.tn, another ? MT_DISAPPEARTOANOTHER : MT_APPEARTOANOTHER); }
static void gm_title_update(void)
{
    switch (nrv_begin(&gm.tn)) {
    case MT_HIDE: gm.title_on = 0; break;
    case MT_DISPLAY: if (FIRST(&gm.tn)) lyt_start(gm.title, "TitleWait", 0); break;
    case MT_APPEARTOANOTHER:
        if (FIRST(&gm.tn)) { lyt_start(gm.title, "TitleAppear", 0); lyt_set_text_rec(gm.title, "TextTitle", msg_get(gm.title_msg)); }
        if (lyt_stopped(gm.title, 0)) nrv_set(&gm.tn, MT_DISPLAY);
        break;
    case MT_DISAPPEARTOANOTHER: /* tryDisappear */
        if (FIRST(&gm.tn)) lyt_start(gm.title, "TitleEnd", 0);
        if (!FIRST(&gm.tn) && lyt_stopped(gm.title, 0)) nrv_set(&gm.tn, MT_APPEARTOANOTHER);
        break;
    }
    nrv_end(&gm.tn);
}

static void gm_map_appear(void) /* GalaxyMap::appear: its icons appear (syncStatus), the star count */
{
    gm_sync_icons(); gm_sync_extra();
    lyt_show_pane(gm.title, "Guidance", gm.guidance);
    lyt_set_number_rec(gm.map, "Star", gd_star_num());
}
static void gm_force_view(int galaxy_view) /* forceToGalaxyMap / forceToAstroMap: the transition at its end */
{
    const char *a = galaxy_view ? "DomeIn" : "DomeOut";
    gm_map_appear();
    lyt_start(gm.map, a, 0);
    lyt_set_frame(gm.map, 0, lyt_anim_frames(gm.map, a)); lyt_set_rate(gm.map, 0, 0);
    gm_title_start(galaxy_view ? "GalaxyMap_GrandGalaxyMap" : "GalaxyMap_AstroMap", 0);
    gm.mv.next = -1; nrv_set(&gm.mv, MV_IDLE);
}
static void gm_change_view(int galaxy_view) { gm_map_appear(); nrv_set(&gm.mv, galaxy_view ? MV_FADEINGALAXY : MV_FADEINASTRO); } /* changeToGalaxyMap / changeToAstroMap */
static int gm_transition(void) { return nrv_is(&gm.mv, MV_FADEINGALAXY) || nrv_is(&gm.mv, MV_FADEINASTRO); }

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

static int gm_pointed_icon(void) /* getPointingGalaxyIcon: the first icon pointed (shown, its pane not scaled to nothing) */
{
    float x, y;
    game_cursor(&x, &y);
    for (int i = 0; i < gm.nicon; i++)
        if (gm.icon[i].status && gm.icon[i].l_scale > 0.01f && lyt_pane_hit(gm.icon[i].l, "GalaxyIcon", x, y)) return i;
    return -1;
}

static int gm_try_focus_in(void) /* GalaxyMap::tryFocusIn */
{
    if (!gm.focus_on) { gm.pointing = -1; nrv_set(&gm.mv, MV_IDLE); return 1; } /* _71 false */
    int p = gm_pointed_icon();
    if (p != gm.pointing) {
        if (p >= 0) se_start(SE_SY_GALAMAP_ICON_POINT, NULL);
        gm.pointing = p;
        nrv_set(&gm.mv, p >= 0 ? MV_FOCUSIN : MV_FOCUSOUT);
        return 1;
    }
    if (p >= 0 && pad_pressed(PAD_CROSS)) { pad_eat(PAD_CROSS); nrv_set(&gm.mv, MV_DETAIL); return 1; }
    return 0;
}
static void gm_map_update(void) /* GalaxyMap's nerves */
{
    switch (nrv_begin(&gm.mv)) {
    case MV_IDLE: gm_try_focus_in(); break;
    case MV_FOCUSIN: /* onPointing, the plate */
        if (gm.pointing >= 0) { btn_try_pointing(&gm.icon[gm.pointing].b, 1); gm.icon[gm.pointing].b.pointing = 1; gm_plate(gm.pointing); }
        gm_try_focus_in();
        break;
    case MV_FOCUSOUT:
        if (gm_try_focus_in()) break;
        if (gm.mv.step > 30) nrv_set(&gm.mv, MV_IDLE); /* sFocusKeepFrame */
        break;
    case MV_FADEINGALAXY: case MV_FADEINASTRO: {
        int in = nrv_is(&gm.mv, MV_FADEINGALAXY);
        if (FIRST(&gm.mv)) { lyt_start(gm.map, in ? "DomeIn" : "DomeOut", 0); gm_title_start(in ? "GalaxyMap_GrandGalaxyMap" : "GalaxyMap_AstroMap", 1); }
        if (lyt_stopped(gm.map, 0)) { gm.pointing = -1; nrv_set(&gm.mv, MV_IDLE); }
        break;
    }
    case MV_DETAIL: /* GalaxyMapGalaxyDetail: Appear, Display (A: Disappear), End, dead */
        if (FIRST(&gm.mv)) {
            const GmIcon *ic = &gm.icon[gm.pointing];
            int state = ic->status == 1 ? 2 : 3; /* startDisplay(name, isStatusNew) */
            gm_pic(state == 3 ? ic->gal : "GalaxyInfoTextureUnknown");
            dmu_info_fill(gm.detail, ic->gal, state);
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

/* PowerStarList: the opened galaxies (sorted by their kind), a row each: short name, its stars got, best coins, crown;
 * a belt (separator) behind each kind's rows; two pages laid out at once, TurnOverPage1 / 2 scroll them */
static int gm_prio(const char *g) /* getSortPriority */
{
    int nn = 0;
    for (int i = 1; i <= gd_scenario_num(g); i++) nn += !gd_scenario_comet(g, i)[0];
    if (nn == 3) return 1;
    if (!strcmp(g, "HeavensDoorGalaxy")) return 2;
    for (int i = 1; i <= gd_power_star_num(g); i++) if (gd_is_grand_star(g, i)) return 3;
    return gd_question_galaxy(g) ? 4 : 5;
}
static void gm_list_build(void) /* getSortedList (galaxies only) */
{
    gm.nlist = 0;
    for (int i = 0; i < gm.nicon && gm.nlist < 75; i++) /* (the table's order: ScenarioData's) */
        if (gd_power_star_num(gm.icon[i].gal) && gd_flag_is_on(gm.icon[i].gal)) gm.list_gal[gm.nlist++] = gm.icon[i].gal;
    for (int i = 1; i < gm.nlist; i++) /* a stable insertion sort by priority */
        for (int k = i; k > 0 && gm_prio(gm.list_gal[k]) < gm_prio(gm.list_gal[k - 1]); k--) {
            const char *t = gm.list_gal[k]; gm.list_gal[k] = gm.list_gal[k - 1]; gm.list_gal[k - 1] = t;
        }
}
static void gm_col_add(u16 *col, int *n, const u16 *s, int newline)
{
    if (newline && *n < 126) col[(*n)++] = 0x0A;
    for (; s && *s && *n < 126; s++) col[(*n)++] = *s;
    col[*n] = 0;
}
static const char *const gm_belt[4] = {"PicBelt1", "PicBelt2", "PicBelt3", "PicBelt4"};      /* cSeparatorPaneTable */
static const char *const gm_belt_size[4] = {"PicBelt1", "PicBelt2", "PicBelt3", "WinBelt4"}; /* cSeparatorSizePaneTable */
static int gm_show_belt(int i, float h) /* tryShowSeparator: its height (setSeparatorPaneSize) */
{
    if (i < 0 || lyt_pane_shown(gm.list, gm_belt[i])) return 0;
    lyt_show_rec(gm.list, gm_belt[i], 1);
    lyt_set_pane_height(gm.list, gm_belt_size[i], h);
    return 1;
}
static void gm_list_fill(int page, int single) /* updateList: rows 0..14 (single) or 0..29 */
{
    static const char *const stage[8] = {"TxtStage1", "TxtStage2", "TxtStage3", "TxtStage4", "TxtStage5", "TxtStage6", "TxtStage7", "TxtStage8"};
    static const char *const star[4] = {"TxtStar1", "TxtStar2", "TxtStar3", "TxtStar4"}, *const coin[4] = {"TxtCoinPiece1", "TxtCoinPiece2", "TxtCoinPiece3", "TxtCoinPiece4"},
                             *const comp[4] = {"TxtComp1", "TxtComp2", "TxtComp3", "TxtComp4"}, *const tm[4] = {"TxtTime1", "TxtTime2", "TxtTime3", "TxtTime4"};
    if (page >= 0) gm.page = page;
    int len[5][8];
    memset(len, 0, sizeof len);
    for (int c = 0; c < 5; c++) for (int k = 0; k < 8; k++) gm.col[c][k][0] = 0;
    for (int k = 0; k < 4; k++) lyt_show_rec(gm.list, gm_belt[k], 0);
    gm_list_build();
    gm.npages = (gm.nlist - 1) / 15 + 1;
    if (gm.npages < 1) gm.npages = 1;
    int odd = gm.page % 2, limit = single ? 15 : 30, row = odd, prev = -1, rows = 0, count = 0;
    for (int it = gm.page * 15; it < gm.nlist; it++) {
        const char *g = gm.list_gal[it];
        char label[48];
        u16 buf[40];
        int si = row / 4, ci = row / 8, sl = row % 4, cl = row % 8, n = 0;
        if (si >= 8 || ci >= 4) break;
        snprintf(label, sizeof label, "GalaxyNameShort_%s", g);
        gm_col_add(gm.col[0][si], &len[0][si], msg_get(label), sl != 0);
        for (int s = 1; s <= gd_power_star_num(g) && n < 36; s++) /* makeStarNumText */
            if (gd_has_star(g, s)) {
                int code = gd_is_grand_star(g, s) ? 'K' : gd_is_special_star(g, s, "SpecialStarGreen") ? 'O' :
                           gd_is_special_star(g, s, "SpecialStarRed") ? 'S' : gd_scenario_comet(g, s)[0] ? 'e' : '7';
                buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = (u16)(code - '0'); buf[n++] = 0;
            }
        buf[n] = 0;
        gm_col_add(gm.col[1][ci], &len[1][ci], buf, cl != 0);
        n = 0; /* makeCoinText */
        buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = 'A' - '0'; buf[n++] = 0;
        int best = gd_max_coins(g, 1);
        for (int d = 100; d; d /= 10) buf[n++] = (u16)('0' + best / d % 10);
        buf[n] = 0;
        gm_col_add(gm.col[2][ci], &len[2][ci], buf, cl != 0);
        n = 0; /* makeCrownText */
        if (gd_star_num_galaxy(g) == gd_power_star_num(g)) { buf[n++] = 0x1A; buf[n++] = 3; buf[n++] = 'P' - '0'; buf[n++] = 0; }
        buf[n] = 0;
        gm_col_add(gm.col[3][ci], &len[3][ci], buf, cl != 0);
        gm_col_add(gm.col[4][ci], &len[4][ci], NULL, cl != 0);
        int pr = gm_prio(g), sep = pr <= 1 ? 0 : pr <= 3 ? 1 : 2; /* the separators: a belt from a kind's first row */
        if (prev != sep) {
            if (prev >= 0) gm_show_belt(prev, 20.0f * rows); /* cSeparatorOffset */
            gm.belt[sep][0] = 0; gm.belt[sep][1] = 20.0f * row;
            prev = sep; rows = 0;
        }
        rows++; count++;
        row++;
        if (row - odd >= limit) break;
    }
    if (count % 15 > 0) rows += 15 - count % 15;
    gm_show_belt(prev, 20.0f * rows);
    for (int k = 0; k < 8; k++) lyt_set_text_rec(gm.list, stage[k], gm.col[0][k]);
    for (int k = 0; k < 4; k++) {
        lyt_set_text_rec(gm.list, star[k], gm.col[1][k]); lyt_set_text_rec(gm.list, coin[k], gm.col[2][k]);
        lyt_set_text_rec(gm.list, comp[k], gm.col[3][k]); lyt_set_text_rec(gm.list, tm[k], gm.col[4][k]);
    }
}
static void gm_list_buttons(void) /* updateButtonAppearance */
{
    int one = gm.npages == 1;
    lyt_show_pane(gm.list, "ArrowDownButton", !one); lyt_show_pane(gm.list, "ArrowUpButton", !one); lyt_show_pane(gm.list, "PageNumber", !one);
    gm.down.active = !one && gm.page < gm.npages - 1;
    gm.up.active = !one && gm.page > 0;
    if (one) return;
    lyt_set_pane_alpha(gm.list, "ArrowDownButton", gm.down.active ? -1 : 80);
    lyt_set_pane_alpha(gm.list, "ArrowUpButton", gm.up.active ? -1 : 80);
    u16 a[2][4];
    const u16 *args[2] = {a[0], a[1]};
    for (int k = 0; k < 2; k++) {
        char d[12];
        snprintf(d, sizeof d, "%d", k ? gm.npages : gm.page + 1);
        int i = 0;
        for (; d[i]; i++) a[k][i] = (u16)d[i];
        a[k][i] = 0;
    }
    msg_with_args(gm.page_txt, 48, msg_get("AllStarList_Page"), args, 2);
    lyt_set_text_rec(gm.list, "PageNumber", gm.page_txt);
}
static void gm_list_scroll(int set) /* startScrollAnimNext (set: at frame 0, stopped) */
{
    static const char *const panes[4] = {"List1", "List2", "PicBG", "Belt"};
    const char *a = gm.page % 2 ? "TurnOverPage2" : "TurnOverPage1";
    for (int k = 0; k < 4; k++) {
        lyt_start_pane(gm.list, panes[k], a, 0);
        if (set) lyt_set_pane_frame_stop(gm.list, panes[k], 0, 0.0f);
    }
}
static void gm_list_scroll_prev(void) /* startScrollAnimPrev */
{
    static const char *const panes[4] = {"List1", "List2", "PicBG", "Belt"};
    for (int k = 0; k < 4; k++) lyt_start_pane_reverse(gm.list, panes[k], gm.page % 2 ? "TurnOverPage2" : "TurnOverPage1", 0);
}
static void gm_list_appear(void) /* PowerStarList::appear */
{
    gm_list_fill(0, 1);
    gm_list_scroll(1);
    gm.list_on = 1;
    gm.pl.next = -1; nrv_set(&gm.pl, PL_APPEAR);
}
static void gm_list_update(void)
{
    switch (nrv_begin(&gm.pl)) {
    case PL_APPEAR:
        if (FIRST(&gm.pl)) {
            btn_appear(&gm.up); gm.up.active = 0;
            btn_appear(&gm.down); gm.down.active = 0;
            gm_list_buttons();
            lyt_start(gm.list, "Appear", 0);
        }
        if (lyt_stopped(gm.list, 0)) { gm.up.active = gm.down.active = 1; nrv_set(&gm.pl, PL_WAIT); }
        break;
    case PL_WAIT:
        if (FIRST(&gm.pl)) { gm_list_scroll(1); gm_list_fill(-1, 0); gm_list_buttons(); }
        if (gm.page < gm.npages - 1 && (btn_try_select(&gm.down) || pad_pressed(PAD_DOWN))) nrv_set(&gm.pl, PL_PAGENEXT);
        else if (gm.page > 0 && (btn_try_select(&gm.up) || pad_pressed(PAD_UP))) nrv_set(&gm.pl, PL_PAGEPREV);
        break;
    case PL_PAGENEXT: case PL_PAGEPREV:
        if (FIRST(&gm.pl)) {
            if (nrv_is(&gm.pl, PL_PAGENEXT)) { gm_list_scroll(0); gm.page++; }
            else { gm_list_fill(gm.page - 1, 0); gm_list_scroll_prev(); }
            se_start(SE_SY_GALAMAP_SCROLL, NULL);
        }
        if (lyt_pane_stopped(gm.list, "List1", 0) && lyt_pane_stopped(gm.list, "List2", 0)) { /* resetButtonAll */
            btn_force_wait(&gm.up); btn_force_wait(&gm.down);
            nrv_set(&gm.pl, PL_WAIT);
        }
        break;
    }
    nrv_end(&gm.pl);
    btn_update(&gm.up); btn_update(&gm.down); /* control */
    if (btn_pointing_trigger(&gm.up) || btn_pointing_trigger(&gm.down)) se_start(SE_SY_GALAMAP_CURSOR_ON, NULL);
}

int menu_test_point_map(const char *pane) /* tests: the pointer on the map's select button / back button / a galaxy's icon */
{
    float x, y;
    for (int i = 0; i < gm.nicon; i++)
        if (!strcmp(gm.icon[i].gal, pane)) {
            if (!lyt_pane_center(gm.icon[i].l, "GalaxyIcon", &x, &y)) return 0;
            game_set_cursor(x, y);
            return 1;
        }
    if (!lyt_pane_center(!strcmp(pane, "BoxLButton") ? gm.btn_l : gm.back_l, pane, &x, &y)) return 0;
    game_set_cursor(x, y);
    return 1;
}
static void gm_wipe_close_now(void) { wipe_start(1, 1, gm.wipe_rgb); wipe.t = 1; } /* WipeFade::forceClose */
void menu_galaxy_map(int mode) /* GalaxyMapController::start*: GalaxyMap's setMode*, the fader, the first nerve */
{
    if (!gm.map || !nrv_is(&gm.n, GM_OFF)) return;
    gm.mode = mode;
    gm.icon_mode = mode == GMAP_NEW_GALAXY || mode == GMAP_CHALLENGE; /* GalaxyMapIcon::setModeNewGalaxyDiscover */
    gm.tico_mode = mode == GMAP_NEW_TICO;                              /* GalaxyMapTicoIcon::setModeDiscover */
    gm.dome_mode = mode == GMAP_NEW_DOME;                              /* GalaxyMapDomeIcon::setModeNewDomeDiscover */
    gm.guidance = gm.focus_on = mode == GMAP_ASTRO || mode == GMAP_GALAXY; /* setModeNormal: Guidance, _71 */
    gm.wipe_rgb = mode == GMAP_TICO_COMET ? 0xFFFFFF : 0; /* setFaderParam: sTicoCometPrayWipeColor / sWipeColor */
    gm.wipe_frames = mode == GMAP_TICO_COMET ? 60 : 12;   /* sTicoCometFadeinoutFrame / sMapFadeinoutFrame */
    gm.n.next = -1;
    if (mode == GMAP_NEW_DOME) { gm_wipe_close_now(); nrv_set(&gm.n, GM_FADEINMODEMAP); }
    else if (mode == GMAP_TICO_COMET) { gm_wipe_close_now(); nrv_set(&gm.n, GM_TC_WAITWHITE); }
    else nrv_set(&gm.n, GM_READYMODEMAP);
}
int menu_galaxy_map_active(void) { return !nrv_is(&gm.n, GM_OFF); }

static int gm_back(void) /* tryPushBackButton */
{
    if (gm.back_clicked) return 1;
    if (pad_pressed(PAD_CIRCLE) || pad_pressed(PAD_TRIANGLE)) { se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); return 1; }
    return 0;
}
static int gm_try_change_mode(void) /* tryChangeMode */
{
    if (gm.btn_on && btn_is(&gm.button, BP_DECIDEDWAIT) && gm.button.selected) { gm.button.selected = 0; gm.mode = GMAP_LIST; return 1; } /* isDecidedList */
    if ((gm.btn_on && pointing_on(gm.btn_l, "BoxLButton")) || gm.pointing >= 0 || (gm.back_on && pointing_on(gm.back_l, "BoxButton")) ||
        !pad_pressed(PAD_CROSS))
        return 0;
    if (gm.mode == GMAP_ASTRO) { se_start(SE_SY_GALAMAP_PAGE_CHANGE, NULL); se_start(SE_SY_GALAMAP_CH_PITCH_UP, NULL); gm.mode = GMAP_GALAXY; return 1; }
    if (gm.mode == GMAP_GALAXY) { se_start(SE_SY_GALAMAP_PAGE_CHANGE, NULL); se_start(SE_SY_GALAMAP_CH_PITCH_DOWN, NULL); gm.mode = GMAP_ASTRO; return 1; }
    return 0;
}
static void gm_next_from_map(void) /* setNerveAccordingToNextModeFromMap */
{
    nrv_set(&gm.n, gm.mode == GMAP_LIST ? GM_READYLIST : gm.mode == GMAP_GALAXY ? GM_APPEARGALAXY : GM_APPEARASTRO);
}
static int gm_display_and_key_wait(void) /* tryDisplayAndKeyWait */
{
    if (FIRST(&gm.n)) gm.focus_on = 0;
    if (gm.n.step > 30 && pad_pressed(PAD_CROSS)) { nrv_set(&gm.n, GM_FADEOUT); return 1; } /* sDisplayKeepFrame */
    return 0;
}
static int gm_try_appear_galaxy(void) /* tryAppearGalaxyMap */
{
    if (FIRST(&gm.n)) gm_change_view(1);
    return !FIRST(&gm.n) && !gm_transition();
}

static void gm_update(void)
{
    switch (nrv_begin(&gm.n)) {
    case GM_READYMODEMAP:
        if (FIRST(&gm.n)) wipe_start(gm.wipe_frames, 1, gm.wipe_rgb);
        if (!wipe_active()) nrv_set(&gm.n, GM_FADEINMODEMAP);
        break;
    case GM_FADEINMODEMAP:
        if (FIRST(&gm.n)) {
            wipe_start(gm.wipe_frames, 0, gm.wipe_rgb);
            if (gm.mode == GMAP_ASTRO || gm.mode == GMAP_GALAXY) { /* the back button, the select button (startAstroMap) */
                btn_appear(&gm.back); gm.back_clicked = 0; gm.back_on = 1;
                btn_appear(&gm.button); gm.btn_on = 1;
            }
            gm_force_view(gm.mode == GMAP_GALAXY || gm.mode == GMAP_TICO_COMET);
            lyt_start(gm.bg, "Wait", 0);
            gm.pointing = -1; gm.list_on = 0;
        }
        if (!wipe_active()) {
            static const u8 next[8] = {GM_DISPLAYASTRO, GM_DISPLAYGALAXY, GM_DISPLAYASTRO, GM_NGD_ASTRO, GM_NTGD_ASTRO, GM_NDD_ASTRO, GM_TC_PRAY, GM_CGD_ASTRO};
            nrv_set(&gm.n, next[gm.mode & 7]);
        }
        break;
    case GM_APPEARGALAXY:
        if (gm_try_appear_galaxy()) nrv_set(&gm.n, GM_DISPLAYGALAXY);
        break;
    case GM_APPEARASTRO:
        if (FIRST(&gm.n)) gm_change_view(0);
        if (!FIRST(&gm.n) && !gm_transition()) nrv_set(&gm.n, GM_DISPLAYASTRO);
        break;
    case GM_DISPLAYGALAXY: case GM_DISPLAYASTRO:
        if (FIRST(&gm.n)) gm.focus_on = 1;
        if (!gm.detail_on && gm_back()) nrv_set(&gm.n, GM_FADEOUT);
        else if (gm_try_change_mode()) gm_next_from_map();
        break;
    case GM_READYLIST:
        if (FIRST(&gm.n)) { wipe_start(12, 1, 0); gm.focus_on = 0; btn_disappear(&gm.button); } /* changeToStarList */
        if (!wipe_active()) nrv_set(&gm.n, GM_APPEARLIST);
        break;
    case GM_APPEARLIST:
        if (FIRST(&gm.n)) { wipe_start(12, 0, 0); gm_list_appear(); gm.title_on = 0; gm.plate_show = 0; } /* the map killed */
        if (nrv_is(&gm.pl, PL_APPEAR)) break; /* isAppearing */
        if (!wipe_active()) nrv_set(&gm.n, GM_DISPLAYLIST);
        break;
    case GM_DISPLAYLIST:
        if (gm_back()) { gm.mode = GMAP_ASTRO; nrv_set(&gm.n, GM_DISAPPEARLIST); }
        break;
    case GM_DISAPPEARLIST:
        if (FIRST(&gm.n)) wipe_start(12, 1, 0);
        if (!wipe_active()) { /* the select button back (startAstroMap), the list killed, the map forced */
            gm.list_on = 0; gm.pl.next = -1; nrv_set(&gm.pl, PL_OFF);
            gm_force_view(gm.mode == GMAP_GALAXY);
            nrv_set(&gm.n, GM_FADEINMODEMAP);
        }
        break;
    case GM_NGD_ASTRO:
        if (gm.n.step > 1) { gm.mode = GMAP_GALAXY; nrv_set(&gm.n, GM_NGD_APPEARGALAXY); } /* sWaitForNewGalaxyDiscoverPhaseAstroMap */
        break;
    case GM_NGD_APPEARGALAXY:
        if (gm_try_appear_galaxy()) nrv_set(&gm.n, GM_NGD_DISPLAYGALAXY);
        break;
    case GM_NGD_DISPLAYGALAXY: case GM_CGD_ASTRO:
        gm_display_and_key_wait();
        gm.lv = se_level(SE_SY_LV_GALAMAP_BLINK_NEW, NULL, gm.lv);
        break;
    case GM_NTGD_ASTRO:
        gm_display_and_key_wait();
        gm.lv = se_level(SE_SY_LV_GALAMAP_BLINK_FAT, NULL, gm.lv);
        break;
    case GM_NDD_ASTRO:
        gm_display_and_key_wait();
        gm.lv = se_level(SE_SY_LV_GALAMAP_BLINK_DOME, NULL, gm.lv);
        break;
    case GM_TC_FADEOUTWHITE:
        if (FIRST(&gm.n)) { wipe_start(gm.wipe_frames, 1, gm.wipe_rgb); se_start(SE_DM_TICOCOMET_MORPH_WIPE_OUT, NULL); }
        if (!wipe_active()) nrv_set(&gm.n, GM_TC_WAITWHITE);
        break;
    case GM_TC_WAITWHITE:
        if (gm.n.step == 60) nrv_set(&gm.n, GM_FADEINMODEMAP); /* sTicoCometWaitFrame */
        break;
    case GM_TC_PRAY:
        gm_display_and_key_wait();
        break;
    case GM_FADEOUT:
        if (FIRST(&gm.n)) { gm.wipe_rgb = 0; wipe_start(12, 1, 0); }
        if (!wipe_active()) nrv_set(&gm.n, GM_READYGAME);
        break;
    case GM_READYGAME:
        if (FIRST(&gm.n)) { /* killAllLayout */
            gm.back_on = gm.btn_on = gm.title_on = gm.list_on = gm.detail_on = gm.plate_show = 0;
            gm.pl.next = -1; nrv_set(&gm.pl, PL_OFF);
            wipe_start(12, 0, 0);
        }
        if (!wipe_active()) nrv_set(&gm.n, GM_OFF);
        break;
    }
    nrv_end(&gm.n);
    if (nrv_is(&gm.n, GM_OFF) || nrv_is(&gm.n, GM_READYMODEMAP) || nrv_is(&gm.n, GM_TC_WAITWHITE) || nrv_is(&gm.n, GM_READYGAME)) return;
    if (!gm.list_on) {
        gm_map_update();
        lyt_update(gm.map); lyt_update(gm.bg);
        if (gm.title_on) { gm_title_update(); lyt_update(gm.title); }
        lyt_evaluate(gm.map);
        for (int i = 0; i < gm.nicon; i++) {
            if (!gm.icon[i].status) continue;
            float p[3];
            lyt_pane_place(gm.map, gm.icon[i].pane, p);
            gm.icon[i].l_scale = p[2];
            gm_follow(gm.icon[i].l, gm.icon[i].pane);
            if (!gm.icon_mode && !nrv_is(&gm.mv, MV_DETAIL)) btn_update(&gm.icon[i].b); /* control: the normal mode's */
            lyt_update(gm.icon[i].l);
        }
        for (int d = 0; d < 6; d++) if (gm.dome[d]) { char p[8]; snprintf(p, sizeof p, "Dome%d", d + 1); gm_follow(gm.dome[d], p); lyt_update(gm.dome[d]); }
        for (int k = 0; k < 2; k++) /* GalaxyMapMarioIcon::control: at its pane, its alpha */
            if (gm.mario[k]) { gm_follow(gm.mario[k], gm.mario_pane[k]); lyt_set_alpha(gm.mario[k], lyt_pane_alpha(gm.map, gm.mario_pane[k])); lyt_update(gm.mario[k]); }
        for (int k = 0; k < gm.ntico; k++) if (gm.tico_on[k]) { gm_follow(gm.tico[k], gm.tico_pane[k]); lyt_update(gm.tico[k]); }
        for (int i = 0; i < gm.nicon; i++) if (gm.comet_on[i]) { gm_follow(gm.comet[i], gm.icon[i].pane); lyt_update(gm.comet[i]); }
        if (gm.plate_show) { lyt_update(gm.plate); gm.plate_show--; }
        if (gm.detail_on) lyt_update(gm.detail);
    } else if (!nrv_is(&gm.n, GM_DISAPPEARLIST)) { gm_list_update(); lyt_update(gm.list); }
    if (gm.btn_on && !gm.detail_on) { /* GalaxyMapSelectButton: AstroMapWait's trySelect, control */
        if (!gm.list_on && btn_try_select(&gm.button)) se_start(SE_SY_GALAMAP_DECIDE, NULL);
        btn_update(&gm.button); lyt_update(gm.btn_l);
        if (btn_pointing_trigger(&gm.button)) se_start(SE_SY_GALAMAP_CURSOR_ON, NULL);
    }
    if (gm.back_on && !gm.detail_on) {
        if (btn_pointing_trigger(&gm.back)) se_start(SE_SY_BUTTON_CURSOR_ON, NULL);
        if (btn_try_select(&gm.back)) { if (!gm.back_clicked) se_start(SE_SY_GALAXY_DECIDE_CANCEL, NULL); gm.back_clicked = 1; }
        btn_update(&gm.back); lyt_update(gm.back_l);
        if (gm.back_clicked && (nrv_is(&gm.n, GM_DISPLAYLIST) || nrv_is(&gm.n, GM_DISAPPEARLIST))) gm.back_clicked = nrv_is(&gm.n, GM_DISPLAYLIST);
    }
}

static int gm_screen(void) /* deactivateGameSceneDraw3D: from FadeinModeMap to ReadyGame */
{
    return menu_galaxy_map_active() && !nrv_is(&gm.n, GM_READYMODEMAP) && !nrv_is(&gm.n, GM_TC_WAITWHITE) &&
           !nrv_is(&gm.n, GM_TC_FADEOUTWHITE) && !nrv_is(&gm.n, GM_READYGAME);
}
static void gm_draw_icons(int order) /* drawGalaxyIconWithOrder: 1 the rest, 3 the one pointed, 2 new in a discover mode */
{
    for (int i = 0; i < gm.nicon; i++) {
        if (!gm.icon[i].status || gm.icon[i].l_scale <= 0.01f) continue;
        int o = gm_new_on_discover(&gm.icon[i]) ? 2 : i == gm.pointing ? 3 : 1;
        if (o == order) lyt_draw(gm.icon[i].l);
    }
}
static void gm_draw(void) /* GalaxyMapController::draw: the background, the map, the list, the buttons, the plate / detail */
{
    if (!gm_screen()) return;
    render_world_done();
    if (!gm.list_on) {
        lyt_draw(gm.bg); lyt_draw(gm.map); /* GalaxyMap::draw */
        for (int d = 0; d < 6; d++) if (gm.dome[d]) lyt_draw(gm.dome[d]);
        if (gm.mario[0]) lyt_draw(gm.mario[0]);
        gm_draw_icons(1);
        if (gm.title_on) lyt_draw(gm.title);
        for (int k = 0; k < gm.ntico; k++) if (gm.tico_on[k]) lyt_draw(gm.tico[k]);
        if (gm.mario[1]) lyt_draw(gm.mario[1]);
        gm_draw_icons(3);
        for (int i = 0; i < gm.nicon; i++) if (gm.comet_on[i]) lyt_draw(gm.comet[i]);
        gm_draw_icons(2);
    } else lyt_draw(gm.list);
    if (gm.back_on) lyt_draw(gm.back_l);
    if (gm.btn_on) lyt_draw(gm.btn_l);
    if (!gm.list_on && gm.plate_show) lyt_draw(gm.plate); /* drawGalaxyInfo */
    if (!gm.list_on && gm.detail_on) lyt_draw(gm.detail);
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
    for (int k = 0; k < 4; k++) lyt_follow(gm.list, gm_belt[k], gm.belt[k], 1); /* setFollowPos, setFollowTypeAdd */
    lyt_set_text_rec(gm.list, "Title", msg_get("AllStarList_Title"));
    lyt_show_rec(gm.list, "Capture", 0); lyt_show_rec(gm.list, "Photo", 0); /* (no capture) */
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
    for (int i = 0; i < gm.nicon; i++) { /* initPaneCtrlPointing: a comet icon for a galaxy with any comet (isExistAnyComet) */
        int any = 0;
        for (int k = 1; k <= gd_scenario_num(gm.icon[i].gal); k++) any |= gd_scenario_comet(gm.icon[i].gal, k)[0] != 0;
        if (any && (gm.comet[i] = lyt_new("IconComet"))) lyt_start(gm.comet[i], "Wait", 0);
    }
    { /* initMarioIcon: cMarioIconPositions / cMarioIconPositionsInDome by the dome's scenario (0 outside a dome) */
        static const char *const pos[7] = {"Mario7", "Mario1", "Mario2", "Mario3", "Mario4", "Mario5", "Mario6"};
        int no = 0;
        if (!strncmp(game_stage_name, "AstroDome/", 10)) no = atoi(game_stage_name + 10);
        if (no < 0 || no > 6) no = 0;
        snprintf(gm.mario_pane[0], sizeof gm.mario_pane[0], "%s", pos[no]);
        if (no) snprintf(gm.mario_pane[1], sizeof gm.mario_pane[1], "GMario%d", no);
        for (int k = 0; k < 1 + (no != 0); k++)
            if ((gm.mario[k] = lyt_new("IconMario"))) { lyt_start(gm.mario[k], "Luigi", 1); lyt_set_frame(gm.mario[k], 1, 0); lyt_set_rate(gm.mario[k], 1, 0); }
    }
    for (gm.ntico = 0; gm.ntico < 8; gm.ntico++) { /* initTicoIcon: calcExclamationGalaxyNum, panes Tico<n> */
        const char *g;
        int given, max;
        if (!gd_tico_galaxy(gm.ntico, &g, &given, &max)) break;
        snprintf(gm.tico_pane[gm.ntico], sizeof gm.tico_pane[0], "Tico%d", gm.ntico + 1);
        if ((gm.tico[gm.ntico] = lyt_new("MapTicoIcon"))) lyt_start(gm.tico[gm.ntico], "Appear", 0);
    }
    gm.pointing = -1;
    gm.tn.next = -1; gm.pl.next = -1;
    gm_pic_init();
    printf("galaxy map: %d galaxy icons, %d detail pictures\n", gm.nicon, gmp.n);
}

/* --- MissLayout ("TooBad"), WipeKoopa, WipeGameOver ("GameOver"): the miss / game over sequences' (game.c) ----- */
static struct { LytInst *toobad, *koopa, *gameover, *cinema; u8 tb_on, ko_on, go_on, ci_on, ci_frame; } mlay;

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
void menu_wipe(int close, int frames, u32 rgb) { wipe_start(frames < 0 ? 30 : frames, close, rgb); } /* WipeFade::sFadeFrame */
int menu_wipe_closed(void) { return wipe.frames && wipe.close && wipe.t >= wipe.frames; }
void menu_wipe_force(int close) { wipe_start(1, close, wipe.rgb); wipe.t = 1; if (!close) wipe.frames = 0; }
/* CinemaFrame.cpp: Screen (dead), ScreenToFrame (Appear), Frame (Wait), FrameToScreen (End, then dead) */
void menu_cinema(int frame)
{
    if (!mlay.cinema || (mlay.ci_on && mlay.ci_frame == frame)) return;
    if (!frame && !mlay.ci_on) return;
    mlay.ci_on = 1; mlay.ci_frame = (u8)frame;
    lyt_start(mlay.cinema, frame ? "Appear" : "End", 0);
}
static void mlay_init(void)
{
    memset(&mlay, 0, sizeof mlay);
    mlay.toobad = lyt_new("TooBad");
    mlay.koopa = lyt_new("WipeKoopa");
    mlay.gameover = lyt_new("GameOver");
    mlay.cinema = lyt_new("CinemaFrame");
}
static void mlay_update(void)
{
    if (mlay.tb_on) lyt_update(mlay.toobad);
    if (mlay.ko_on) lyt_update(mlay.koopa);
    if (mlay.go_on) lyt_update(mlay.gameover);
    if (mlay.ci_on) {
        lyt_update(mlay.cinema);
        if (lyt_stopped(mlay.cinema, 0)) { /* setNerveAtAnimStopped: Frame (Wait) or Screen (killed) */
            if (mlay.ci_frame) lyt_start(mlay.cinema, "Wait", 0);
            else mlay.ci_on = 0;
        }
    }
}
static void mlay_draw(void)
{
    if (!mlay.tb_on && !mlay.ko_on && !mlay.go_on && !mlay.ci_on) return;
    render_world_done();
    if (mlay.ci_on) lyt_draw(mlay.cinema); /* DrawType_CinemaFrame: under the other layouts */
    if (mlay.tb_on) lyt_draw(mlay.toobad);
    if (mlay.ko_on) lyt_draw(mlay.koopa);
    if (mlay.go_on) lyt_draw(mlay.gameover);
}

/* --- RosettaMonologue (RosettaDemoAstroDome.cpp: "PrologueStarSteward", map.bin): the picture book of -----------
 * ロゼッタ状況説明デモ. appear: FadeIn, the balloon's WinFadeIn, RosettaMonologue000; control: A turns the message's
 * pages (SE_SY_TALK_FOCUS_ITEM), then the next message (CrossFade1..3 on its picture); after the fourth SE_SY_TALK_OK,
 * FadeOut and WinFadeOut, and once stopped forceCloseWipeFade and dead. Its IconAButton (openWithoutMessage once the
 * text is out, at AButtonPosition, term at A). ponytail: the page's text at once (no TalkTextFormer typewriter), no
 * CS_CLICK_CLOSE */
static void mono_form(const u16 *s) /* formMessage: one page (MessageUtil's page tag: group 1 type 1) into Text00 */
{
    int n = 0;
    mono.page = NULL;
    for (; *s && n < 399;) {
        if (*s == 0x1A) {
            int len = 4 + (s[3] + 1) / 2, page = s[1] == 1 && s[2] == 1;
            if (page) { s += len; mono.page = *s == '\n' ? s + 1 : s; if (!*mono.page) mono.page = NULL; break; }
            while (len-- && n < 399) mono.buf[n++] = *s++;
        } else mono.buf[n++] = *s++;
    }
    mono.buf[n] = 0;
    lyt_set_text_rec(mono.l, "Text00", mono.buf);
}
static void mono_msg(void) /* the message mMessageNo */
{
    static const u16 none[1];
    char label[32];
    snprintf(label, sizeof label, "RosettaMonologue%03d", mono.no);
    const u16 *m = msg_get(label);
    mono_form(m ? m : none);
}
void menu_monologue_start(void) /* RosettaMonologue::appear */
{
    if (!mono.l) return;
    mono.on = 1; mono.no = 0;
    lyt_start(mono.l, "FadeIn", 0);
    lyt_start_pane(mono.l, "TalkBalloon", "WinFadeIn", 0);
    mono_msg();
}
int menu_monologue_alive(void) { return mono.on; }
static void mono_update(void) /* RosettaMonologue::control */
{
    if (!mono.on) return;
    Iab *ab = mono_ab();
    if (mono.no >= 4) {
        if (lyt_stopped(mono.l, 0)) { menu_wipe_force(1); mono.on = 0; return; } /* forceCloseWipeFade, kill */
    } else {
        if (!ab->on) iab_open(ab); /* isTextAppearedAll (at once here): openWithoutMessage (iab_update: setLayoutPosAtPaneTrans) */
    }
    if (mono.no < 4 && pad_pressed(PAD_CROSS)) { /* testCorePadTriggerA */
        iab_term(ab);
        if (mono.page) { se_start(SE_SY_TALK_FOCUS_ITEM, NULL); mono_form(mono.page); } /* nextPage */
        else {
            mono.no++;
            if (mono.no == 4) se_start(SE_SY_TALK_OK, NULL);
            else { se_start(SE_SY_TALK_FOCUS_ITEM, NULL); mono_msg(); }
            switch (mono.no) {
            case 1: lyt_start_pane(mono.l, "CrossFade1", "CrossFade1", 0); break;
            case 2: lyt_start_pane(mono.l, "CrossFade2", "CrossFade2", 0); break;
            case 3: lyt_start_pane(mono.l, "CrossFade3", "CrossFade3", 0); break;
            case 4: lyt_start(mono.l, "FadeOut", 0); lyt_start_pane(mono.l, "TalkBalloon", "WinFadeOut", 0); break;
            }
        }
    }
    lyt_update(mono.l);
    iab_update(ab);
}
static struct Iab *mono_ab(void) /* RosettaDemoAstroDome's mIconAButton (killed at first) */
{
    static Iab ab;
    if (mono.l && ab.host != mono.l) iab_init(&ab, mono.l, "AButtonPosition");
    return &ab;
}

/* --- StaffRoll (StaffRoll.cpp: StaffRollLine x 64, StaffRollPicture, StaffRollTitle, InformationMessage; ending.bin) --
 * appear (the 3D scene not drawn): PrepareDemo, PrepareBgm (STM_STAFF_ROLL, 60 steps), TitleLogoAppear (240),
 * TitleLogoScroll (up to -210, the pictures start), LineScroll (StaffList000's lines, one each time the last has moved
 * up its leading: 24.5, an empty line's 29.5; each from the screen's bottom at 0.5 a frame, gone above -24.5, columns at
 * the centre +- 60 / 200 by '|'), EndLogoScroll (TheEnd up to 220 once the BGM's count is at 14211 less its scroll),
 * AllRights (the thank-you voice at 190), PrepareInfo (100), then Pause for the ending movie (isPauseOrEnd); startInfo:
 * Restart, the information windows (picture book unless ViewNormalEnding, comet, Luigi at the first complete ending),
 * the save, End. The US region's params (sStaffRollParamTable[1]). ponytail: the programmed demos "StaffRoll" /
 * "SaveAfterStaffRoll" not started (nothing else runs here), the count runs from the BGM's start (not isPlayingStageBgm),
 * no BGM_NEW_GALAXY sub BGM (sequenced), no IconAButton under the information window, MessageEditorMessageTag lengths as
 * the talk text's */
#define SRL_LINES 64
enum { SRL_PREPAREDEMO, SRL_PREPAREBGM, SRL_TITLEAPPEAR, SRL_TITLESCROLL, SRL_LINESCROLL, SRL_ENDSCROLL, SRL_ALLRIGHTS,
       SRL_PREPAREINFO, SRL_BOOKIN, SRL_COMETIN, SRL_LUIGIIN, SRL_BOOKOUT, SRL_COMETOUT, SRL_LUIGIOUT, SRL_PAUSE,
       SRL_RESTART, SRL_PREPARESAVE, SRL_SAVE, SRL_END };
static const struct { int step, tex, pane, anim; } srl_pics[] = { /* sPictureTimingTable */
    {1100, 0, 2, 0},   {1900, 0, 2, 1},   {2000, 1, 1, 0},   {2800, 1, 1, 1},   {2900, 2, 0, 0},   {3700, 2, 0, 1},   {3800, 3, 3, 0},
    {4600, 3, 3, 1},   {4700, 4, 2, 0},   {5500, 4, 2, 1},   {6100, 5, 0, 2},   {6600, 5, 0, 3},   {6100, 6, 3, 2},   {6600, 6, 3, 3},
    {6700, 7, 1, 2},   {7200, 7, 1, 3},   {6700, 8, 2, 2},   {7200, 8, 2, 3},   {7300, 9, 0, 2},   {7800, 9, 0, 3},   {7300, 10, 3, 2},
    {7800, 10, 3, 3},  {7900, 11, 1, 2},  {8300, 11, 1, 3},  {7900, 12, 2, 2},  {8300, 12, 2, 3},  {8500, 13, 0, 2},  {8900, 13, 0, 3},
    {8500, 14, 3, 2},  {8900, 14, 3, 3},  {9400, 15, 3, 0},  {10200, 15, 3, 1}, {10300, 16, 2, 0}, {11100, 16, 2, 1}, {11200, 17, 1, 0},
    {12000, 17, 1, 1}, {12090, 18, 0, 0}, {12890, 18, 0, 1}, {12990, 19, 3, 0}, {13390, 19, 3, 1}};
static const char *const srl_panes[4] = {"PictureLU", "PictureRU", "PictureLD", "PictureRD"};
static const char *const srl_childs[4] = {"PicLU", "PicRU", "PicLD", "PicRD"};
static const char *const srl_anims[4] = {"FadeIn", "FadeOut", "FrameIn", "FrameOut"};
#define SRL_SPEED 0.5f       /* mScrollSpeed (Us) */
#define SRL_LEADING 24.5f    /* mLineLeading */
#define SRL_EMPTY 29.5f      /* mEmptyLineLeading */
static struct {
    LytInst *title, *pic, *info, *line[SRL_LINES];
    u8 on, line_on[SRL_LINES], pic_on;
    int line_step[SRL_LINES], pic_step;
    float line_pos[SRL_LINES][2], title_pos[2], end_pos[2];
    u16 line_text[SRL_LINES][128], info_text[1];
    const u16 *msg;
    int nlines, cur_line, gen, count;  /* mMsgLineNum, mMsgLine, _48, _4C */
    Nrv n;
} srl;

static void srl_init(void) /* StaffRoll::init (the layouts are in the ending's stage only) */
{
    memset(&srl, 0, sizeof srl);
    if (!(srl.title = lyt_new("StaffRollTitle")) || !(srl.pic = lyt_new("StaffRollPicture")) || !(srl.info = lyt_new("InformationWindow"))) {
        srl.title = NULL;
        return;
    }
    for (int i = 0; i < SRL_LINES; i++) {
        if (!(srl.line[i] = lyt_new("StaffRoll"))) { srl.title = NULL; return; }
        lyt_follow(srl.line[i], "Text00", srl.line_pos[i], 0); /* setFollowPos */
        lyt_set_pane_scale(srl.line[i], "Text00", 0.8f);         /* mPaneScale */
    }
    lyt_copy_pane_trans(srl.title, "TitleLogo", srl.title_pos);
    lyt_follow(srl.title, "TitleLogo", srl.title_pos, 0);
    lyt_show_pane(srl.title, "TitleLogo", 0);
    lyt_copy_pane_trans(srl.title, "TheEnd", srl.end_pos);
    lyt_follow(srl.title, "TheEnd", srl.end_pos, 0);
    lyt_show_pane(srl.title, "TheEnd", 0);
    srl.msg = msg_get("StaffList000"); /* getGameMessageDirect, countMessageLine */
    srl.nlines = srl.msg ? 1 : 0;
    for (const u16 *s = srl.msg; s && *s; s++) srl.nlines += *s == '\n';
    for (int i = 0; i < 4; i++) { /* StaffRollPicture::disappearAllPane */
        lyt_start_pane(srl.pic, srl_panes[i], "FadeIn", 0);
        lyt_set_pane_frame_stop(srl.pic, srl_panes[i], 0, 0.0f);
    }
}
static const u16 *srl_line(int k) /* MR::getMessageLine: the k-th line's start */
{
    const u16 *s = srl.msg;
    while (s && k > 0 && *s) if (*s++ == '\n') k--;
    return s;
}
static void srl_make_line(void) /* StaffRoll::makeLine: its columns by '|', each on a dead StaffRollLine */
{
    const u16 *s = srl_line(srl.cur_line);
    u16 cols[3][128];
    int nc = 0, n = 0;
    for (;;) {
        if (*s == 0x1A) { int len = 4 + (s[3] + 1) / 2; while (len-- && n < 127) cols[nc][n++] = *s++; continue; } /* a tag kept whole */
        if (*s != '|' && *s != '\n' && *s) { if (n < 127) cols[nc][n++] = *s; s++; continue; }
        cols[nc][n] = 0;
        nc++; n = 0;
        if (*s != '|' || nc == 3) break;
        s++;
    }
    for (int c = 0; c < nc; c++) {
        float x = 304.0f; /* getScreenWidth / 2 */
        if (nc == 2) x += c == 0 ? -60.0f : 60.0f;     /* sDoubleInterval */
        else if (nc == 3 && c) x += c == 1 ? -200.0f : 200.0f; /* sTripleInterval */
        int i = 0;
        while (i < SRL_LINES && srl.line_on[i]) i++;
        if (i == SRL_LINES) return;
        memcpy(srl.line_text[i], cols[c], sizeof cols[c]);
        lyt_set_text_rec(srl.line[i], "Text00", srl.line_text[i]);
        srl.line_pos[i][0] = x;
        srl.line_on[i] = 1; srl.line_step[i] = 0; /* appear: Scroll */
    }
}
static int srl_next_empty(void) { return srl.cur_line < srl.nlines && (*srl_line(srl.cur_line) == '\n' || !*srl_line(srl.cur_line)); }
static void srl_next_info(void) /* setNrvNextInfo */
{
    int c = srl.n.cur;
    if (c == SRL_PREPAREINFO) nrv_set(&srl.n, SRL_PAUSE);
    else if (!gd_flag_is_on("ViewNormalEnding") && c == SRL_RESTART) nrv_set(&srl.n, SRL_BOOKIN);
    else if (c == SRL_BOOKOUT) { gd_flag_try_on("ViewNormalEnding"); nrv_set(&srl.n, SRL_COMETIN); }
    else if (!gd_flag_is_on("ViewCompleteEnding") && gd_flag_is_on("PowerStarComplete")) { nrv_set(&srl.n, SRL_LUIGIIN); gd_flag_try_on("ViewCompleteEnding"); }
    else nrv_set(&srl.n, SRL_PREPARESAVE);
}
void menu_staffroll(int ev) /* StorySequenceExecutor::exeStaffRollSequence: 12 appear, 13 startInfo */
{
    if (!srl.title) { printf("staff roll: no layouts\n"); return; }
    srl.on = 1;
    if (ev == 12) { nrv_set(&srl.n, SRL_PREPAREDEMO); srl.n.cur = -1; } /* appear */
    else { srl.n.cur = -1; nrv_set(&srl.n, SRL_RESTART); }               /* startInfo */
    printf("staff roll: %s\n", ev == 12 ? "appear" : "info");
}
int menu_staffroll_pause_or_end(void) { return !srl.title || srl.n.cur == SRL_PAUSE || srl.n.cur == SRL_END; } /* isPauseOrEnd */
int menu_staffroll_on(void) { return srl.on; }
static void srl_update(void)
{
    if (!srl.on) return;
    switch (nrv_begin(&srl.n)) {
    case SRL_PREPAREDEMO: nrv_set(&srl.n, SRL_PREPAREBGM); break; /* tryStartDemo "StaffRoll" */
    case SRL_PREPAREBGM:
        if (FIRST(&srl.n)) game_bgm("STM_STAFF_ROLL");
        if (srl.n.step > 60) { srl.count = 0; nrv_set(&srl.n, SRL_TITLEAPPEAR); } /* sStepForTitleLogoAppear */
        break;
    case SRL_TITLEAPPEAR:
        if (FIRST(&srl.n)) { lyt_show_pane(srl.title, "TitleLogo", 1); lyt_start_pane(srl.title, "TitleLogo", "TitleLogoAppear", 0); }
        if (srl.n.step == 240) nrv_set(&srl.n, SRL_TITLESCROLL);
        break;
    case SRL_TITLESCROLL:
        if (FIRST(&srl.n)) { lyt_start_pane(srl.title, "TitleLogo", "TitleLogoWait", 0); srl.pic_on = 1; srl.pic_step = 0; srl.gen = 0; }
        srl.title_pos[1] -= SRL_SPEED;
        if (srl.title_pos[1] < -210.0f) { lyt_show_pane(srl.title, "TitleLogo", 0); nrv_set(&srl.n, SRL_LINESCROLL); }
        break;
    case SRL_LINESCROLL: { /* until the end logo's scroll lands it at sEndLogoPosY as the BGM's 14211th step comes */
        float h = 456.0f - 220.0f;
        int rem = fmodf(h, SRL_SPEED) != 0.0f, steps = (int)(h / SRL_SPEED);
        if (srl.count >= 14211 - (rem + steps)) nrv_set(&srl.n, SRL_ENDSCROLL);
        break;
    }
    case SRL_ENDSCROLL:
        if (FIRST(&srl.n)) {
            lyt_show_pane(srl.title, "TheEnd", 1);
            lyt_start_pane(srl.title, "TheEnd", "AllRights", 0);
            lyt_set_pane_frame_stop(srl.title, "TheEnd", 0, 0.0f);
            srl.end_pos[1] = 456.0f;
        }
        srl.end_pos[1] -= SRL_SPEED;
        if (srl.end_pos[1] < 220.0f) { srl.end_pos[1] = 220.0f; nrv_set(&srl.n, SRL_ALLRIGHTS); }
        break;
    case SRL_ALLRIGHTS:
        if (FIRST(&srl.n)) lyt_start_pane(srl.title, "TheEnd", "AllRights", 0);
        if (srl.n.step == 190) se_start(SE_SY_E3_THANK_YOU, NULL); /* sStepToThankYouVoice (Mario) */
        if (lyt_pane_stopped(srl.title, "TheEnd", 0)) { srl.pic_on = 0; lyt_show_pane(srl.title, "TheEnd", 0); nrv_set(&srl.n, SRL_PREPAREINFO); }
        break;
    case SRL_PREPAREINFO: if (srl.n.step == 100) srl_next_info(); break; /* sStepForPrepareInfo */
    case SRL_BOOKIN: case SRL_COMETIN: case SRL_LUIGIIN: /* exeInfoAppear */
        if (FIRST(&srl.n)) {
            int c = srl.n.cur;
            const u16 *m = msg_get(c == SRL_BOOKIN ? "System_Result005" : c == SRL_COMETIN ? "StaffRollInfo_Comet" : "StaffRollInfo_Luigi");
            if (c == SRL_LUIGIIN) se_start(SE_SY_LUIGI_MODE_APPEAR, NULL);
            lyt_show_rec(srl.info, "InfoWindowUp", 0); lyt_show_rec(srl.info, "InfoWindowCenter", 1); /* setCenter(true) */
            lyt_set_text_rec(srl.info, "InfoWindowCenter", m ? m : srl.info_text);
            lyt_start(srl.info, "Appear", 0);
            lyt_start(srl.info, "Line", 1);
            lyt_set_frame(srl.info, 1, lyt_text_height(srl.info, "InfConfirmC")); /* setAnimFrameAndStopAdjustTextHeight */
            lyt_set_rate(srl.info, 1, 0);
        }
        if (pad_pressed(PAD_CROSS)) { /* testSystemPadTriggerDecide */
            se_start(SE_SY_TALK_OK, NULL);
            nrv_set(&srl.n, srl.n.cur + 3); /* its Disappear */
        }
        break;
    case SRL_BOOKOUT: case SRL_COMETOUT: case SRL_LUIGIOUT: /* exeInfoDisappear */
        if (FIRST(&srl.n)) lyt_start(srl.info, "End", 0);
        if (srl.n.step > 0 && lyt_stopped(srl.info, 0)) srl_next_info();
        break;
    case SRL_PAUSE: break;
    case SRL_RESTART: srl_next_info(); break;
    case SRL_PREPARESAVE: nrv_set(&srl.n, SRL_SAVE); break;
    case SRL_SAVE:
        if (FIRST(&srl.n)) menu_save_start(1, 0, 0); /* startGameDataSaveSequence(true, false) */
        else if (!menu_saving()) nrv_set(&srl.n, SRL_END);
        break;
    case SRL_END: break;
    }
    nrv_end(&srl.n);
    /* movement: the lines, the pictures, the count, tryGenerateLine */
    for (int i = 0; i < SRL_LINES; i++) { /* StaffRollLine::exeScroll */
        if (!srl.line_on[i]) continue;
        if (srl.line_step[i]++ == 0) srl.line_pos[i][1] = 456.0f; /* getScreenHeight */
        srl.line_pos[i][1] -= SRL_SPEED;
        if (srl.line_pos[i][1] < -SRL_LEADING) srl.line_on[i] = 0;
        lyt_update(srl.line[i]);
    }
    if (srl.pic_on) { /* StaffRollPicture::exeWork */
        int complete = gd_flag_is_on("PowerStarComplete");
        for (unsigned i = 0; i < sizeof srl_pics / sizeof srl_pics[0]; i++) {
            if (srl_pics[i].step != srl.pic_step) continue;
            char t[24];
            snprintf(t, sizeof t, "%cstaffrolltexture%03d", complete ? 'l' : 'm', srl_pics[i].tex); /* LStaffRollTexture / MStaffRollTexture */
            lyt_replace_pane_texture(srl.pic, srl_childs[srl_pics[i].pane], lyt_material_texture(srl.pic, t));
            lyt_start_pane(srl.pic, srl_panes[srl_pics[i].pane], srl_anims[srl_pics[i].anim], 0);
        }
        srl.pic_step++;
    }
    if (srl.n.cur >= SRL_TITLEAPPEAR) srl.count++;
    if (srl.gen >= 0 && srl.n.cur >= SRL_TITLESCROLL) { /* tryGenerateLine */
        if (srl.gen == 0 && srl.cur_line < srl.nlines) { srl_make_line(); srl.cur_line++; }
        srl.gen++;
        if (srl.gen * SRL_SPEED >= (srl_next_empty() ? SRL_EMPTY : SRL_LEADING))
            srl.gen = srl.cur_line < srl.nlines ? 0 : -1;
    }
    lyt_update(srl.title);
    lyt_update(srl.pic);
    lyt_update(srl.info);
    if (srl.n.cur == SRL_END) srl.on = 0;
}
static void srl_draw(void) /* StaffRoll::draw: black, the information, the pictures, the lines, the title */
{
    float eye[3] = {0, 0, 1000.0f}, target[3] = {0, 0, 0}, up[3] = {0, 1.0f, 0};
    render_begin(eye, target, up);
    render_world_done();
    if (srl.n.cur >= SRL_BOOKIN && srl.n.cur <= SRL_LUIGIOUT) lyt_draw(srl.info);
    if (srl.pic_on) lyt_draw(srl.pic);
    for (int i = 0; i < SRL_LINES; i++) if (srl.line_on[i]) lyt_draw(srl.line[i]);
    lyt_draw(srl.title);
    if (sv.win.alive) lyt_draw(sv.win.l);
    wipe_draw_quad();
    fade_draw();
}

static void wipe_update(void)
{
    if (!wipe.frames) return;
    if (wipe.t < wipe.frames) wipe.t++;
    else if (!wipe.close) wipe.frames = 0;
}

void menu_draw(void)
{
    if (!pm.alive && !sr.alive) { subm_draw(); hm_draw(); } /* the game's default layout (MarioSubMeter, MarioMeter) */
    if (sr.alive) { render_world_done(); lyt_draw(sr.l); if (sr.info && (sr.n.cur == SR_INFO || sr.n.cur == SR_INFOEND)) lyt_draw(sr.info); }
    if (dmu.on) { render_world_done(); dmu_draw(); }
    mlay_draw();
    if (spg.l && !nrv_is(&spg.n, SPG_ENDWAIT)) { render_world_done(); lyt_draw(spg.l); } /* StarPointerGuidance */
    if (mono.on) { render_world_done(); lyt_draw(mono.l); iab_draw(mono_ab()); }
    gm_draw();
    if (pm.alive) {
        render_world_done(); /* the scene under the menu (LayoutOnPause draws after everything) */
        lyt_draw(pm.l);
        if (pm.win.alive) lyt_draw(pm.win.l);
    }
    llt_draw(); /* PauseMenu::draw: the letter after the menu (and KinopioAstro's) */
    if (sv.win.alive) { render_world_done(); lyt_draw(sv.win.l); } /* the save's window over all of it */
    wipe_draw_quad();
    fade_draw();
}
