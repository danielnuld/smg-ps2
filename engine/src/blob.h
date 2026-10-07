#ifndef BLOB_H
#define BLOB_H

#include <tamtypes.h>

/* Read-only views into the slice blob (format: tools/slice.py header). Nothing is copied. */
typedef struct {
    u16 type, zone;
    s32 l_id, path;
    float pos[3], rot[3], scale[3];
    s32 arg[8];
} Row; /* 80 bytes, matches slice.py ROW */

typedef struct {
    u32 nverts;          /* 3 per triangle */
    u16 tex;             /* TEXT index, NO_TEX for untextured */
    const s16 *pos;      /* nverts x s16 (x, y, z in steps of Model.qscale, w = 4 * matrix slot), 16-byte aligned */
    const u32 *col;      /* nverts x u8 (r, g, b, a), 16-byte aligned */
    const s16 *uv;       /* nverts x s16 (s, t), 12 fraction bits; NULL if untextured */
} VuGroup;

#define NO_TEX 0xFFFF

typedef struct {
    u16 w, h;
    u8 psm, clamp_s, clamp_t, alpha; /* alpha: 0 opaque, 1 cut-out, 2 blended */
    u32 pix_off, pix_size, clut_off; /* from the TEXT payload; CLUT = 16 x u32 */
    u8 tw, th;                       /* log2 of w, h */
    u8 pad[10];
} TexEntry; /* 32 bytes, matches slice.py TEXT */

/* SKIN chunk (tools/slice.py skin_entry): bind pose, draw matrices, matrix table per VUMD group. */
typedef struct { s16 parent; u16 pad; float scale[3], rot[3], trans[3]; } SkJoint;   /* rot in radians */
typedef struct { u16 first, count; } SkDrw;  /* count 0: rigid, first = joint; else mix[first .. first+count) */
typedef struct { u16 joint, pad; float weight; float invbind[12]; } SkMix;          /* invbind: 3x4 rows */
typedef struct { u8 nmat, drw[11]; } SkGroup;
typedef struct {
    u16 njoints, ndrw, ngroups, nmix;
    const SkJoint *joints;
    const SkDrw *drw;
    const SkMix *mix;
    const SkGroup *groups;
} Skin;

/* ANIM chunk: zlib-compressed little-endian clips (tools/slice.py anim_clip). */
typedef struct { const char *name; u32 raw, zsize; const u8 *z; } Clip;

typedef struct {
    const char *name;
    const Skin *skin;    /* NULL for static models */
    u32 vu_ngroups;
    VuGroup *vu;
    float radius; /* bounding sphere around the origin, for culling */
    float qscale; /* position quantisation step, folded into the matrices */
    void *chain;  /* static models: prebuilt VIF chain of all batches, ends with RET (render.c) */
} Model;

#define SCENE_MAX_MODELS 512
#define SCENE_MAX_TEX 1024
#define SCENE_MAX_PACKS 64
typedef struct { const TexEntry *e; const u8 *base; } TexRef;

typedef struct {
    u32 ntypes;
    const char *types; /* ntypes * 32 */
    u32 nobjs, nstarts, nmodels;
    const Row *objs, *starts;
    Model *models;
    u32 nkcl;
    const u8 *kcl_chunk[SCENE_MAX_PACKS]; /* KCL chunks: {u32 n}, n x {char[32] model, u32 size, bytes, pad 4} */
    u32 nclips;
    Clip *clips;
    u32 ntex;
    TexRef *tex;        /* scene-wide texture index -> entry and its TEXT payload (offsets are from there) */
} Scene;

/* Placement chunks (TYPE OBJS STRT) and the blob's assets. The scene must start zeroed. */
int blob_load(Scene *s, const u8 *blob);
/* Assets of one more pack (MODL VUMD SKIN ANIM TEXT KCL): models appended, texture indices made scene-wide. */
int blob_add(Scene *s, const u8 *blob);
/* Payload of a chunk by tag (NULL if absent). */
const u8 *blob_chunk(const u8 *blob, const char *tag, u32 *size);
int scene_type(const Scene *s, const char *name);
/* Mario's start row: the first start in zone 1 (the slice's Mysterious zone), else the first. */
const Row *scene_start(const Scene *s);
const Model *scene_model(const Scene *s, const char *name);
/* Little-endian KCL of a model (planets), NULL if it has none. */
const u8 *scene_kcl(const Scene *s, const char *name);

#endif
