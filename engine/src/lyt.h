#ifndef LYT_H
#define LYT_H

#include "blob.h"

/* Runtime 2D layouts (tools/lyt.py LYTR): Petari's LayoutActor / LayoutManager over nw4r lyt. An instance plays
 * BRLAN animations on channels (MR::startAnim: the whole layout) or on a pane and its children
 * (MR::startPaneAnim), panes are shown / hidden and text boxes given text by name; lyt_draw draws it over the frame. */
typedef struct LytInst LytInst;

void lyt_init(const Scene *s); /* the scene's LYTR chunks (game_init) */
LytInst *lyt_new(const char *layout);                                 /* in the scene arena; NULL if not there */
int lyt_start(LytInst *l, const char *anim, int ch);                  /* MR::startAnim; 0 if it has no such anim */
int lyt_start_pane(LytInst *l, const char *pane, const char *anim, int ch); /* MR::startPaneAnim */
void lyt_set_frame(LytInst *l, int ch, float frame);                 /* MR::setAnimFrame (the layout channel's) */
void lyt_set_pane_frame(LytInst *l, const char *pane, int ch, float frame);
void lyt_set_rate(LytInst *l, int ch, float rate);                   /* MR::setAnimRate */
void lyt_set_pane_rate(LytInst *l, const char *pane, int ch, float rate); /* MR::setPaneAnimRate */
int lyt_stopped(const LytInst *l, int ch);                           /* MR::isAnimStopped */
int lyt_pane_stopped(const LytInst *l, const char *pane, int ch);    /* MR::isPaneAnimStopped */
float lyt_frame(const LytInst *l, int ch);
void lyt_update(LytInst *l);                                         /* a frame on (LayoutActor::movement) */
void lyt_draw(LytInst *l);
int lyt_pane(const LytInst *l, const char *pane);                    /* its index, -1 none */
void lyt_show_pane(LytInst *l, const char *pane, int show);          /* MR::showPane / hidePane */
int lyt_pane_shown(const LytInst *l, const char *pane);             /* !MR::isHiddenPane */
void lyt_hide_for_good(LytInst *l, const char *pane);          /* hidden, its animations' visibility ignored */
void lyt_set_text(LytInst *l, const char *pane, const u16 *text);    /* MR::setTextBoxMessage... (kept: not copied) */
int lyt_pane_center(LytInst *l, const char *pane, float *x, float *y); /* its centre on the screen (0..640, 0..448) */
void lyt_set_pos(LytInst *l, float x, float y);                      /* the root's offset in layout units */
void lyt_set_alpha(LytInst *l, float a);                             /* the whole layout's alpha (0..1) */
void lyt_show_rec(LytInst *l, const char *pane, int show);           /* MR::showPaneRecursive / hidePaneRecursive */
void lyt_set_text_rec(LytInst *l, const char *pane, const u16 *text); /* MR::setTextBoxMessageRecursive */
void lyt_set_number_rec(LytInst *l, const char *pane, int n);        /* MR::setTextBoxNumberRecursive */
void lyt_set_vpos_rec(LytInst *l, const char *pane, int v);          /* setTextBoxVerticalPosition*Recursive: 0 top 1 centre 2 bottom */
void lyt_set_pane_frame_stop(LytInst *l, const char *pane, int ch, float frame); /* MR::setPaneAnimFrameAndStop */
float lyt_pane_frame(const LytInst *l, const char *pane, int ch);   /* MR::getPaneAnimFrame */
float lyt_anim_frames(const LytInst *l, const char *anim);          /* MR::getAnimFrameMax */
int lyt_pane_hit(LytInst *l, const char *pane, float x, float y);    /* the screen point (0..640, 0..448) on the pane */
/* MR::setFollowPos + setFollowTypeReplace (add 0: the pane's place is *pos) / setFollowTypeAdd (1: moved by *pos);
 * *pos in the layout's screen units (608 x 456, top left 0, y down), read each frame; NULL stops following */
void lyt_follow(LytInst *l, const char *pane, const float *pos, int add);
void lyt_copy_pane_trans(LytInst *l, const char *pane, float out[2]); /* MR::copyPaneTrans: its place, those units */
float lyt_text_height(LytInst *l, const char *pane);                /* its text boxes' tallest text (layout units) */
float lyt_text_width(LytInst *l, const char *pane);                 /* its text boxes' widest text (layout units) */

/* game.c: a string's width (draw 0) or the string drawn at screen (x, y top) in a font (0 MessageFont26, 1 NumberFont,
 * 2 PictureFont: each character its icon) and size (layout units), its colour 0xRRGGBB and alpha 0..1 */
float game_text(const u16 *s, int font, float x, float y, float fsx, float fsy, float space, u32 rgb, float alpha, int draw);

#endif
