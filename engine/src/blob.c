#include "blob.h"
#include "mem.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const u8 *blob_chunk(const u8 *blob, const char *tag, u32 *size)
{
    u32 n = *(const u32 *)(blob + 8);
    const u8 *p = blob + 16; /* format v3: 16-byte headers, payloads 16-byte aligned (tools/slice.py) */
    for (u32 i = 0; i < n; i++) {
        u32 sz = *(const u32 *)(p + 4);
        if (memcmp(p, tag, 4) == 0) {
            if (size)
                *size = sz;
            return p + 16;
        }
        p += 16 + sz;
    }
    return NULL;
}

int blob_load(Scene *s, const u8 *blob)
{
    if (memcmp(blob, "SMGS", 4) != 0 || *(const u32 *)(blob + 4) != 3)
        return -1;
    const u8 *t = blob_chunk(blob, "TYPE", NULL), *o = blob_chunk(blob, "OBJS", NULL), *st = blob_chunk(blob, "STRT", NULL);
    const u8 *m = blob_chunk(blob, "MODL", NULL), *k = blob_chunk(blob, "KCL ", NULL);
    s->ntypes = *(const u32 *)t;
    s->types = (const char *)(t + 4);
    s->nobjs = *(const u32 *)o;
    s->objs = (const Row *)(o + 4);
    s->nstarts = *(const u32 *)st;
    s->starts = (const Row *)(st + 4);

    s->nmodels = *(const u32 *)m;
    s->models = mem_alloc(MEM_RENDER, s->nmodels * sizeof(Model));
    const u8 *p = m + 4;
    for (u32 i = 0; i < s->nmodels; i++) {
        Model *md = &s->models[i];
        md->name = (const char *)p;
        md->ngroups = *(const u32 *)(p + 32);
        md->groups = mem_alloc(MEM_RENDER, md->ngroups * sizeof(Group));
        p += 36;
        float r2 = 0;
        for (u32 g = 0; g < md->ngroups; g++) {
            Group *gr = &md->groups[g];
            gr->nverts = ((const u32 *)p)[0];
            gr->ntris = ((const u32 *)p)[1];
            gr->verts = (const float *)(p + 8);
            p += 8 + gr->nverts * 12;
            gr->idx = (const u16 *)p;
            u32 ib = gr->ntris * 6;
            p += ib + ((4 - ib % 4) % 4);
            for (u32 v = 0; v < gr->nverts; v++) {
                const float *q = gr->verts + 3 * v;
                float d = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
                if (d > r2)
                    r2 = d;
            }
        }
        md->radius = sqrtf(r2);
    }
    const u8 *vm = blob_chunk(blob, "VUMD", NULL);
    if (vm) { /* {u32 n, pad}, n x {char[32] name, u32 ngroups, pad, groups {u32 nverts, u16 tex, pad, pos[], col[], uv[]}} */
        u32 nv = *(const u32 *)vm;
        const u8 *q = vm + 16;
        for (u32 i = 0; i < nv && i < s->nmodels; i++) {
            Model *md = &s->models[i];
            md->vu_ngroups = *(const u32 *)(q + 32);
            md->vu = mem_alloc(MEM_RENDER, md->vu_ngroups * sizeof(VuGroup));
            q += 48;
            for (u32 g = 0; g < md->vu_ngroups; g++) {
                u32 n = *(const u32 *)q, cb = (4 * n + 15) & ~15u;
                VuGroup *vg = &md->vu[g];
                vg->nverts = n;
                vg->tex = *(const u16 *)(q + 4);
                vg->pos = (const float *)(q + 16);
                vg->col = (const u32 *)(q + 16 + 16 * n);
                vg->uv = vg->tex == NO_TEX ? NULL : (const s16 *)(q + 16 + 16 * n + cb);
                q += 16 + 16 * n + cb + (vg->uv ? cb : 0);
            }
        }
    }
    const u8 *tx = blob_chunk(blob, "TEXT", NULL);
    if (tx) { /* {u32 n, pad}, n x TexEntry, then pixels and CLUTs */
        s->ntex = *(const u32 *)tx;
        s->tex = (const TexEntry *)(tx + 16);
        s->tex_base = tx;
    }
    if (k && *(const u32 *)k) {
        s->kcl_size = *(const u32 *)(k + 36);
        s->kcl = k + 40;
    }
    return 0;
}

int scene_type(const Scene *s, const char *name)
{
    for (u32 i = 0; i < s->ntypes; i++)
        if (strcmp(s->types + 32 * i, name) == 0)
            return i;
    return -1;
}

const Model *scene_model(const Scene *s, const char *name)
{
    for (u32 i = 0; i < s->nmodels; i++)
        if (strcmp(s->models[i].name, name) == 0)
            return &s->models[i];
    return NULL;
}
