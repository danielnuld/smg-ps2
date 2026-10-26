#ifndef ANIM_H
#define ANIM_H

#include "blob.h"

#define ANIM_MAX_JOINTS 48 /* DinoPackun 46 */
#define ANIM_MAX_DRW 160  /* DinoPackun 160 */
#define ANIM_TRACKS 4     /* XanimeBckTable4 */

/* Skeletal animation of one skinned model instance (tools/skin.py is the reference implementation). */
typedef struct {
    const Skin *skin;
    const u8 *clip;      /* inflated clip in the cache, NULL = bind pose */
    int clip_id;
    float frame, speed;
    float phase;         /* >= 0: start the next clip at this fraction of its length (loop to loop switch) */
    int loop, done;
    int interp_left;     /* frames left of the cross-fade from the frozen pose (XanimePlayer::updateInterpoleRatio) */
    float interp;        /* 0..1 weight of the new clip */
    float q[ANIM_MAX_JOINTS][4], t[ANIM_MAX_JOINTS][3], s[ANIM_MAX_JOINTS][3];     /* last local pose */
    float fq[ANIM_MAX_JOINTS][4], ft[ANIM_MAX_JOINTS][3], fs[ANIM_MAX_JOINTS][3];  /* frozen pose */
    float world[ANIM_MAX_JOINTS][16];  /* joint world matrices (model space), column-major */
    float drw[ANIM_MAX_DRW][16];       /* draw matrices: rigid = joint world, envelope = sum w * world * invbind */
    int fx_seen;                       /* clip_id + 1 whose effects were last checked (game.c draw_skin), 0 none */
    float fx_frame;                    /* its frame then */
    int ntrack;                        /* > 1: a blend group (XanimeBckTable2-4: 基本, 水泳基本...) of that many tracks */
    int tclip[ANIM_TRACKS];            /* its tracks' clips (0 the clip itself), -1 none */
    float tw[ANIM_TRACKS];             /* their weights (changeTrackWeight; XanimeCore::calcBlend normalises them) */
} Anim;

/* Allocate the clip cache (sized from the scene's largest clip). */
void anim_init(const Scene *s);
void anim_bind(Anim *a, const Skin *skin);
/* Start a clip by name (no-op if it is already playing); returns 0 if the clip is missing. interp > 0: freeze the
 * current pose and fade into the clip over that many frames (Petari's per-animation interpolation, XanimeGroupInfo
 * _8); 0 snaps. */
int anim_play(Anim *a, const char *name, float speed, int loop, int interp);
/* Advance one frame and compute the pose (world + draw matrices). */
void anim_update(Anim *a);
/* Advance one frame only (its model not drawn this frame: its clip's timing goes on, the pose is not computed) */
void anim_advance(Anim *a);
extern float anim_rate; /* logic steps one update stands for (1; main's catch-up sets it around game_draw) */
/* anim_update in two: the joints' world matrices (which may then be changed), then the draw matrices from them */
void anim_pose(Anim *a);
void anim_skin(Anim *a);
/* MR::startBck: the clip from frame 0 (even if it is playing), no interpolation (ModelManager::startBck; interp > 0:
 * startBckWithInterpole), looping as its own BCK says; 0 if missing */
int anim_start(Anim *a, const char *name, int interp);
/* a clip's frame_max and J3D loop mode (0 once, 1 once and reset, 2 loop, 3 mirror once, 4 mirror loop); -1 if missing */
int anim_clip_info(const char *name, int *mode);
/* the second track: clip blended with weight w over the first (anim_play's) at the first's phase; NULL off */
void anim_blend(Anim *a, const char *clip, float w);
/* a blend group: its tracks' clips (clips[0] played as by anim_play, the others at its phase: XanimeCore::updateFrame)
 * and their weights; n 1 ends it */
void anim_group(Anim *a, const char *const *clips, int n, const float *w, float speed, int interp);
/* the group's weights (changeTrackWeight) */
void anim_weights(Anim *a, const float *w);
/* XanimePlayer::changeTrackAnimation: track k of the group plays clip instead */
void anim_track(Anim *a, int k, const char *clip);
/* MarioAnimator's XanimePlayerUpper: joint root and its descendants take up's local pose (its own clip and fade), the
 * world matrices rebuilt (call after both poses; anim_skin after) */
void anim_overlay(Anim *a, const Anim *up, int root);
/* Clip frames (frame_max) of the playing clip, 0 if none. */
int anim_length(const Anim *a);
u32 anim_cache_bytes(void);
/* a VUMD group's BTK (VuGroup.btk) at `frame` -> its UV matrix (3 qwords: columns s, t, 1) */
void anim_btk(const u8 *btk, float frame, float uvm[12]);

#endif
