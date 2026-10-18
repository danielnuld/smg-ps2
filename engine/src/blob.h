#ifndef BLOB_H
#define BLOB_H

#include <tamtypes.h>

/* Read-only views into the slice blob (format: tools/slice.py header). Nothing is copied. */
typedef struct {
    u16 type, zone;
    s32 l_id, path;
    float pos[3], rot[3], scale[3];
    s32 arg[8];
    s16 sw[5];  /* stage switches (SW_APPEAR, SW_A, SW_B, SW_DEAD, SW_SLEEP), -1 none: < 1000 the row's zone's */
    s16 group;  /* GroupId (joinToGroupArray), -1 none */
} Row; /* 92 bytes, matches slice.py ROW */
enum { SW_APPEAR, SW_A, SW_B, SW_DEAD, SW_SLEEP };

typedef struct {
    u32 nverts;          /* 3 per triangle */
    u16 tex;             /* TEXT index, NO_TEX for untextured */
    u16 nbatch;
    u8 blend, zwrite, alpha; /* material: 0 opaque, 1 blended, 2 additive, 3 the frame buffer x alpha (a screen copy
                              * seen through a tint); depth written; constant alpha 0..255 */
    u8 lit;              /* 1 lit material: per-vertex normals, lit on VU1 (render_light); 2 environment-mapped: per-vertex
                          * normals, UVs from them on VU1 (VU1Env, env) */
    const u16 *batch;    /* nbatch x {emitted, unique} */
    const u8 *data;      /* per batch: s16 pos[unique][4] (x, y, z in steps of Model.qscale, w = 7 * matrix slot),
                          * s16 uv[unique][2] (12 fraction bits) if textured, s8 normal[unique][4] (x 127) if lit,
                          * u8 rgbi[emitted][4] (colour + unique index), each padded to 16 (tools/slice.py
                          * index_batches) */
    const u8 *btk;       /* its BTK chunk entry (tools/slice.py btk_entry: after the size word), NULL if none */
    float *uvm;          /* with btk: the UV matrix the chain uploads (3 qwords: columns s, t, 1; anim_btk); env: 4
                          * qwords (VU mem 8..10 and 15, render_env) */
    const float *env;    /* lit 2: rows s, t, q of the texgen on the view normal, columns n.x, n.y, n.z, 1 (tex.env_mtx) */
    u16 swap[4], nswap;  /* its swappable textures (SWAP chunk: Mario's eyelids; [0] its own), scene indices */
} VuGroup;

#define NO_TEX 0xFFFF

typedef struct {
    u16 w, h;
    u8 psm, clamp_s, clamp_t, alpha; /* alpha: 0 opaque, 1 cut-out, 2 blended */
    u32 pix_off, pix_size, clut_off; /* from the TEXT payload; CLUT = 16 x u32 */
    u8 tw, th;                       /* log2 of w, h */
    u8 nmip, pad0;                   /* PSMT4 mip levels after the base in pix (w >> k by h >> k, each padded to 16) */
    s16 lod_k;                       /* GS TEX1 K (1/16): LOD = log2(depth) + K */
    u8 pad[6];
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
    int lit;      /* some group is lit (VuGroup.lit): its matrices carry light matrices */
    void *chain;  /* static models: prebuilt VIF chain of the opaque batches, ends with RET (render.c) */
    void *chain_xlu; /* the same for the alpha-blended batches, drawn after the opaque ones */
} Model;

#define SCENE_MAX_MODELS 512
#define SCENE_MAX_TEX 1024
#define SCENE_MAX_PACKS 64
typedef struct { const TexEntry *e; const u8 *base; } TexRef;

typedef struct {
    u32 ntypes;
    const char *types; /* ntypes * 32 */
    const u8 *type_light; /* TLGT: per type its actor light type (0 Player .. 3 Planet, 255 unknown), or NULL */
    u32 nobjs, nstarts, nmodels;
    const Row *objs, *starts;
    Model *models;
    u32 nkcl;
    const u8 *kcl_chunk[SCENE_MAX_PACKS]; /* KCL chunks: {u32 n}, n x {char[32] model, u32 size, bytes, pad 4} */
    u32 nclips;
    Clip *clips;
    u32 ncanm;           /* CANM chunk: camera animations, per frame pos xyz, watch xyz, twist, fovy */
    const u8 *canm;      /* n x {char[32] name, u32 frames, f32[8 x frames]} */
    u32 ngpos;           /* GPOS chunk: the GeneralPos by name (MR::findNamePos) */
    const u8 *gpos;      /* n x {char[48] name (UTF-8), f32 pos[3], rot[3]} */
    u32 ntex;
    TexRef *tex;        /* scene-wide texture index -> entry and its TEXT payload (offsets are from there) */
    const u8 *ptcl;     /* PTCL chunk (particle effects, tools/jpa.py pack) of the blob that had one, or NULL */
    u32 ptcl_tex0;      /* that blob's first scene texture: PTCL texture indices are relative to it */
    const u8 *eyep;     /* EYEP chunk (Mario's eye patterns, tools/slice.py eye_chunk) after its count, or NULL */
    u32 neyep;
    const u8 *font;     /* FONT chunk (tools/msg.py: the talk font's glyphs) of the blob that had one, or NULL */
    u32 font_tex0;      /* that blob's first scene texture */
    const u8 *talk;     /* TALK chunk (tools/msg.py: the scenario's talk messages), or NULL */
    const u8 *lytr[4];  /* LYTR chunks (tools/lyt.py: runtime layouts) of the blobs that had one */
    u32 lytr_tex0[4];   /* each one's blob's first scene texture */
    u32 nlytr;
    const u8 *msgs;     /* MSGS chunk (tools/lyt.py: the menus' messages by label), or NULL */
    const u8 *gdat;     /* GDAT chunk (tools/lyt.py: story, galaxy and scenario tables), or NULL */
} Scene;

/* Placement chunks (TYPE OBJS STRT) and the blob's assets. The scene must start zeroed. */
int blob_load(Scene *s, const u8 *blob);
/* Assets of one more pack (MODL VUMD SKIN ANIM TEXT KCL): models appended, texture indices made scene-wide. */
int blob_add(Scene *s, const u8 *blob);
/* Payload of a chunk by tag (NULL if absent). */
const u8 *blob_chunk(const u8 *blob, const char *tag, u32 *size);
int scene_type(const Scene *s, const char *name);
/* Mario's start row: MarioNo 0 of zone 0 (Petari cInitializeStartIdInfo), else the first MarioNo 0, else the first. */
const Row *scene_start(const Scene *s);
const Model *scene_model(const Scene *s, const char *name);
/* Little-endian KCL of a model (planets), NULL if it has none. */
const u8 *scene_kcl(const Scene *s, const char *name);

#endif
