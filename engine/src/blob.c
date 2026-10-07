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
    if (memcmp(blob, "SMGS", 4) != 0 || *(const u32 *)(blob + 4) != 4)
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
        md->radius = *(const float *)(p + 32);
        p += 36;
    }
    const u8 *vm = blob_chunk(blob, "VUMD", NULL);
    if (vm) { /* {u32 n, pad}, n x {char[32] name, u32 ngroups, f32 qscale, pad, groups {u32 nverts, u16 tex, pad,
               * pos[], col[], uv[]}} */
        u32 nv = *(const u32 *)vm;
        const u8 *q = vm + 16;
        for (u32 i = 0; i < nv && i < s->nmodels; i++) {
            Model *md = &s->models[i];
            md->vu_ngroups = *(const u32 *)(q + 32);
            md->qscale = *(const float *)(q + 36);
            md->vu = mem_alloc(MEM_RENDER, md->vu_ngroups * sizeof(VuGroup));
            q += 48;
            for (u32 g = 0; g < md->vu_ngroups; g++) {
                u32 n = *(const u32 *)q, cb = (4 * n + 15) & ~15u, pb = (8 * n + 15) & ~15u;
                VuGroup *vg = &md->vu[g];
                vg->nverts = n;
                vg->tex = *(const u16 *)(q + 4);
                vg->pos = (const s16 *)(q + 16);
                vg->col = (const u32 *)(q + 16 + pb);
                vg->uv = vg->tex == NO_TEX ? NULL : (const s16 *)(q + 16 + pb + cb);
                q += 16 + pb + cb + (vg->uv ? cb : 0);
            }
        }
    }
    const u8 *sk = blob_chunk(blob, "SKIN", NULL);
    if (sk) { /* {u32 n}, n x {char[32] model, u16 njoints, ndrw, ngroups, nmix, joints, drw, mix, groups, pad 4} */
        u32 n = *(const u32 *)sk;
        const u8 *q = sk + 4;
        for (u32 i = 0; i < n; i++) {
            Skin *k = mem_alloc(MEM_PLAYER, sizeof(Skin));
            const u16 *h = (const u16 *)(q + 32);
            k->njoints = h[0]; k->ndrw = h[1]; k->ngroups = h[2]; k->nmix = h[3];
            const u8 *p = q + 40;
            k->joints = (const SkJoint *)p; p += k->njoints * sizeof(SkJoint);
            k->drw = (const SkDrw *)p; p += k->ndrw * sizeof(SkDrw);
            k->mix = (const SkMix *)p; p += k->nmix * sizeof(SkMix);
            k->groups = (const SkGroup *)p; p += k->ngroups * sizeof(SkGroup);
            p += (4 - ((u32)(unsigned long)p & 3)) & 3;
            for (u32 m = 0; m < s->nmodels; m++)
                if (strcmp(s->models[m].name, (const char *)q) == 0)
                    s->models[m].skin = k;
            q = p;
        }
    }
    const u8 *an = blob_chunk(blob, "ANIM", NULL);
    if (an) { /* {u32 n}, n x {char[32] name, u32 raw, u32 zsize, z[zsize], pad 4} */
        s->nclips = *(const u32 *)an;
        s->clips = mem_alloc(MEM_PLAYER, s->nclips * sizeof(Clip));
        const u8 *q = an + 4;
        for (u32 i = 0; i < s->nclips; i++) {
            Clip *c = &s->clips[i];
            c->name = (const char *)q;
            c->raw = *(const u32 *)(q + 32);
            c->zsize = *(const u32 *)(q + 36);
            c->z = q + 40;
            q += 40 + ((c->zsize + 3) & ~3u);
        }
    }
    const u8 *tx = blob_chunk(blob, "TEXT", NULL);
    if (tx) { /* {u32 n, pad}, n x TexEntry, then pixels and CLUTs */
        s->ntex = *(const u32 *)tx;
        s->tex = (const TexEntry *)(tx + 16);
        s->tex_base = tx;
    }
    s->kcl_chunk = k;
    return 0;
}

int scene_type(const Scene *s, const char *name)
{
    for (u32 i = 0; i < s->ntypes; i++)
        if (strcmp(s->types + 32 * i, name) == 0)
            return i;
    return -1;
}

const u8 *scene_kcl(const Scene *s, const char *name)
{
    if (!s->kcl_chunk)
        return NULL;
    const u8 *q = s->kcl_chunk + 4;
    for (u32 i = 0, n = *(const u32 *)s->kcl_chunk; i < n; i++) {
        u32 size = *(const u32 *)(q + 32);
        if (strcmp((const char *)q, name) == 0)
            return q + 36;
        q += 36 + ((size + 3) & ~3u);
    }
    return NULL;
}

const Model *scene_model(const Scene *s, const char *name)
{
    for (u32 i = 0; i < s->nmodels; i++)
        if (strcmp(s->models[i].name, name) == 0)
            return &s->models[i];
    return NULL;
}
