#ifndef ANIM_H
#define ANIM_H

#include "blob.h"

#define ANIM_MAX_JOINTS 32
#define ANIM_MAX_DRW 64

/* Skeletal animation of one skinned model instance (tools/skin.py is the reference implementation). */
typedef struct {
    const Skin *skin;
    const u8 *clip;      /* inflated clip in the cache, NULL = bind pose */
    int clip_id;
    float frame, speed;
    int loop, done;
    float world[ANIM_MAX_JOINTS][16];  /* joint world matrices (model space), column-major */
    float drw[ANIM_MAX_DRW][16];       /* draw matrices: rigid = joint world, envelope = sum w * world * invbind */
} Anim;

/* Allocate the clip cache (sized from the scene's largest clip). */
void anim_init(const Scene *s);
void anim_bind(Anim *a, const Skin *skin);
/* Start a clip by name (no-op if it is already playing); returns 0 if the clip is missing. */
int anim_play(Anim *a, const char *name, float speed, int loop);
/* Advance one frame and compute the pose (world + draw matrices). */
void anim_update(Anim *a);
/* Clip frames (frame_max) of the playing clip, 0 if none. */
int anim_length(const Anim *a);
u32 anim_cache_bytes(void);

#endif
