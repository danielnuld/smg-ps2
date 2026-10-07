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
    u32 nverts, ntris;
    const float *verts; /* nverts * 3 */
    const u16 *idx;     /* ntris * 3 */
} Group;

typedef struct {
    u32 nverts;          /* 3 per triangle */
    const float *pos;    /* nverts qwords (x, y, z, 1), 16-byte aligned */
    const u32 *col;      /* nverts qwords (r, g, b, a) */
} VuGroup;

typedef struct {
    const char *name;
    u32 ngroups;
    Group *groups;
    u32 vu_ngroups;
    VuGroup *vu;
    float radius; /* bounding sphere around the origin, for culling */
} Model;

typedef struct {
    u32 ntypes;
    const char *types; /* ntypes * 32 */
    u32 nobjs, nstarts, nmodels;
    const Row *objs, *starts;
    Model *models;
    const u8 *kcl;
    u32 kcl_size;
} Scene;

int blob_load(Scene *s, const u8 *blob);
/* Payload of a chunk by tag (NULL if absent). */
const u8 *blob_chunk(const u8 *blob, const char *tag, u32 *size);
int scene_type(const Scene *s, const char *name);
const Model *scene_model(const Scene *s, const char *name);

#endif
