#include "blob.h"
#include "sequence.h"
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

static int valid(const u8 *blob) { return memcmp(blob, "SMGS", 4) == 0 && *(const u32 *)(blob + 4) == 5; }

int blob_load(Scene *s, const u8 *blob)
{
    if (!valid(blob))
        return -1;
    const u8 *t = blob_chunk(blob, "TYPE", NULL), *o = blob_chunk(blob, "OBJS", NULL), *st = blob_chunk(blob, "STRT", NULL);
    s->ntypes = *(const u32 *)t;
    s->types = (const char *)(t + 4);
    s->type_light = blob_chunk(blob, "TLGT", NULL);
    s->nobjs = *(const u32 *)o;
    s->objs = (const Row *)(o + 4);
    s->nstarts = *(const u32 *)st;
    s->starts = (const Row *)(st + 4);
    return blob_add(s, blob);
}

int blob_add(Scene *s, const u8 *blob)
{
    if (!valid(blob))
        return -1;
    if (!s->models) { /* fixed capacity: game.c keeps Model pointers */
        s->models = mem_alloc(MEM_RENDER, SCENE_MAX_MODELS * sizeof(Model));
        s->tex = mem_alloc(MEM_RENDER, SCENE_MAX_TEX * sizeof(TexRef));
    }
    const u8 *m = blob_chunk(blob, "MODL", NULL), *k = blob_chunk(blob, "KCL ", NULL);
    u32 first = s->nmodels, nm = m ? *(const u32 *)m : 0, tex0 = s->ntex;
    if (first + nm > SCENE_MAX_MODELS || s->nkcl >= SCENE_MAX_PACKS) {
        printf("blob: scene full (%u models)\n", first + nm);
        return -1;
    }
    s->nmodels += nm;
    const u8 *p = m ? m + 4 : NULL;
    for (u32 i = first; i < s->nmodels; i++) {
        Model *md = &s->models[i];
        memset(md, 0, sizeof *md);
        md->name = (const char *)p;
        md->radius = *(const float *)(p + 32);
        p += 36;
    }
    const u8 *vm = blob_chunk(blob, "VUMD", NULL);
    if (vm) { /* {u32 n, pad}, n x {char[32] name, u32 ngroups, f32 qscale, pad, groups {u32 nverts, u16 tex, pad,
               * pos[], col[], uv[]}}; texture indices are per pack, made scene-wide here */
        u32 nv = *(const u32 *)vm;
        const u8 *q = vm + 16;
        for (u32 i = first; i < first + nv && i < s->nmodels; i++) {
            Model *md = &s->models[i];
            md->vu_ngroups = *(const u32 *)(q + 32);
            md->lit = 0;
            md->qscale = *(const float *)(q + 36);
            md->vu = mem_alloc(MEM_RENDER, md->vu_ngroups * sizeof(VuGroup));
            q += 48;
            for (u32 g = 0; g < md->vu_ngroups; g++) {
                VuGroup *vg = &md->vu[g];
                vg->nverts = *(const u32 *)q;
                vg->tex = *(const u16 *)(q + 4);
                vg->nbatch = *(const u16 *)(q + 6);
                vg->blend = q[8];
                vg->zwrite = q[9];
                vg->alpha = q[10];
                vg->lit = q[11];
                vg->nswap = 0;
                md->lit |= vg->lit == 1;
                vg->env = vg->lit == 2 ? (const float *)(q + 16) : NULL;
                if (vg->env) { /* its 12-float matrix after the header (tools/slice.py assets) */
                    q += 48;
                    vg->uvm = mem_alloc(MEM_RENDER, 64);
                    memset(vg->uvm, 0, 64);
                }
                vg->batch = (const u16 *)(q + 16);
                vg->data = q + 16 + ((4 * vg->nbatch + 15) & ~15u);
                q = vg->data;
                for (u32 b = 0; b < vg->nbatch; b++) {
                    u32 ne = vg->batch[2 * b], nu = vg->batch[2 * b + 1];
                    q += ((8 * nu + 15) & ~15u) + (vg->tex != NO_TEX ? (4 * nu + 15) & ~15u : 0) +
                         (vg->lit ? (4 * nu + 15) & ~15u : 0) + ((4 * ne + 15) & ~15u);
                }
                if (vg->tex != NO_TEX)
                    vg->tex += tex0;
            }
        }
    }
    const u8 *bt = blob_chunk(blob, "BTK ", NULL);
    if (bt) { /* {u32 n}, n x {char[32] model, u32 size, u16 group, ...} (tools/slice.py btk_entry) */
        const u8 *q = bt + 4;
        for (u32 i = 0, n = *(const u32 *)bt; i < n; i++) {
            u32 size = *(const u32 *)(q + 32);
            u16 g = *(const u16 *)(q + 36);
            for (u32 j = first; j < s->nmodels; j++)
                if (strcmp(s->models[j].name, (const char *)q) == 0 && g < s->models[j].vu_ngroups &&
                    !s->models[j].vu[g].env) {
                    VuGroup *vg = &s->models[j].vu[g];
                    vg->btk = q + 36;
                    vg->uvm = mem_alloc(MEM_RENDER, 48);
                    vg->uvm[0] = vg->uvm[5] = 1.0f; /* identity until the first anim_btk */
                }
            q += 36 + size;
        }
    }
    const u8 *sw = blob_chunk(blob, "SWAP", NULL);
    if (sw) { /* {u32 n}, n x {char[32] model, u16 group, u16 count, u16 texture[count], pad to 8} (tools/slice.py) */
        const u8 *q = sw + 4;
        for (u32 i = 0, n = *(const u32 *)sw; i < n; i++) {
            u16 g = *(const u16 *)(q + 32), cnt = *(const u16 *)(q + 34);
            const u16 *ids = (const u16 *)(q + 36);
            for (u32 j = first; j < s->nmodels; j++)
                if (strcmp(s->models[j].name, (const char *)q) == 0 && g < s->models[j].vu_ngroups) {
                    VuGroup *vg = &s->models[j].vu[g];
                    vg->nswap = cnt > 4 ? 4 : cnt;
                    for (u32 k = 0; k < vg->nswap; k++) vg->swap[k] = (u16)(ids[k] + tex0);
                }
            u32 len = 36 + 2u * cnt;
            q += len + (-len & 7); /* the entry's padding (to 8) */
        }
    }
    const u8 *sk = blob_chunk(blob, "SKIN", NULL);
    if (sk) { /* {u32 n}, n x {char[32] model, u16 njoints, ndrw, ngroups, nmix, joints, drw, mix, groups, pad 4} */
        u32 n = *(const u32 *)sk;
        const u8 *q = sk + 4;
        for (u32 i = 0; i < n; i++) {
            Skin *sn = mem_alloc(MEM_PLAYER, sizeof(Skin));
            const u16 *h = (const u16 *)(q + 32);
            sn->njoints = h[0]; sn->ndrw = h[1]; sn->ngroups = h[2]; sn->nmix = h[3];
            const u8 *r = q + 40;
            sn->joints = (const SkJoint *)r; r += sn->njoints * sizeof(SkJoint);
            sn->drw = (const SkDrw *)r; r += sn->ndrw * sizeof(SkDrw);
            sn->mix = (const SkMix *)r; r += sn->nmix * sizeof(SkMix);
            sn->groups = (const SkGroup *)r; r += sn->ngroups * sizeof(SkGroup);
            r += (4 - ((u32)(unsigned long)r & 3)) & 3;
            for (u32 j = first; j < s->nmodels; j++)
                if (strcmp(s->models[j].name, (const char *)q) == 0)
                    s->models[j].skin = sn;
            q = r;
        }
    }
    /* every ANIM chunk in turn: an embedded blob holds the placed classes' clips, then Mario's */
    const u8 *cp = blob + 16;
    for (u32 ci = 0, cn = *(const u32 *)(blob + 8); ci < cn; ci++, cp += 16 + *(const u32 *)(cp + 4)) {
        if (memcmp(cp, "ANIM", 4)) continue;
        const u8 *an = cp + 16;
        if (*(const u32 *)an) { /* {u32 n}, n x {char[32] name, u32 raw, u32 zsize, z[zsize], pad 4}: added to the
                                * clips already there (the scenario's placed classes, then Mario's) */
            u32 n0 = s->nclips;
            Clip *old = s->clips;
            s->nclips += *(const u32 *)an;
            s->clips = mem_alloc(MEM_PLAYER, s->nclips * sizeof(Clip));
            if (n0) memcpy(s->clips, old, n0 * sizeof(Clip));
            const u8 *q = an + 4;
            for (u32 i = n0; i < s->nclips; i++) {
                Clip *c = &s->clips[i];
                c->name = (const char *)q;
                c->raw = *(const u32 *)(q + 32);
                c->zsize = *(const u32 *)(q + 36);
                c->z = q + 40;
                q += 40 + ((c->zsize + 3) & ~3u);
            }
        }
    }
    const u8 *ca = blob_chunk(blob, "CANM", NULL);
    if (ca && *(const u32 *)ca) { s->ncanm = *(const u32 *)ca; s->canm = ca + 4; }
    const u8 *tx = blob_chunk(blob, "TEXT", NULL);
    if (tx) { /* {u32 n, pad}, n x TexEntry, then pixels and CLUTs (offsets from the payload) */
        u32 n = *(const u32 *)tx;
        if (s->ntex + n > SCENE_MAX_TEX) {
            printf("blob: too many textures (%u)\n", s->ntex + n);
            return -1;
        }
        for (u32 i = 0; i < n; i++) {
            s->tex[s->ntex].e = (const TexEntry *)(tx + 16) + i;
            s->tex[s->ntex++].base = tx;
        }
    }
    if (k && *(const u32 *)k)
        s->kcl_chunk[s->nkcl++] = k;
    const u8 *ey = blob_chunk(blob, "EYEP", NULL);
    if (ey) { s->neyep = *(const u32 *)ey; s->eyep = ey + 4; }
    const u8 *pt = blob_chunk(blob, "PTCL", NULL);
    if (pt) {
        s->ptcl = pt;
        s->ptcl_tex0 = tex0;
    }
    const u8 *fo = blob_chunk(blob, "FONT", NULL), *tk = blob_chunk(blob, "TALK", NULL);
    if (fo) {
        s->font = fo;
        s->font_tex0 = tex0;
    }
    if (tk)
        s->talk = tk;
    const u8 *ms = blob_chunk(blob, "MSGS", NULL), *gt = blob_chunk(blob, "GDAT", NULL);
    if (ms) s->msgs = ms;
    if (gt) s->gdat = gt;
    const u8 *ly = blob_chunk(blob, "LYTR", NULL);
    if (ly && s->nlytr < 4) {
        s->lytr[s->nlytr] = ly;
        s->lytr_tex0[s->nlytr++] = tex0;
    }
    return 0;
}

const Row *scene_start(const Scene *s)
{
    /* SceneUtil.cpp cInitializeStartIdInfo(0, 0): entering a galaxy, Mario starts at MarioNo 0 (arg0) of zone 0
     * (StageDataHolder::makeMarioJMapInfoIter); the sequence's JMapIdInfo (sequence.c) when it gave one; else the
     * first MarioNo 0, else the first */
    const Row *any = NULL;
    int zone, id = seq_start_id(&zone);
    for (u32 i = 0; id >= 0 && i < s->nstarts; i++)
        if (s->starts[i].arg[0] == id && (int)s->starts[i].zone == zone) return &s->starts[i];
    for (u32 i = 0; i < s->nstarts; i++)
        if (s->starts[i].arg[0] == 0) {
            if (s->starts[i].zone == 0) return &s->starts[i];
            if (!any) any = &s->starts[i];
        }
    return any ? any : &s->starts[0];
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
    for (u32 c = 0; c < s->nkcl; c++) {
        const u8 *q = s->kcl_chunk[c] + 4;
        for (u32 i = 0, n = *(const u32 *)s->kcl_chunk[c]; i < n; i++) {
            u32 size = *(const u32 *)(q + 32);
            if (strcmp((const char *)q, name) == 0)
                return q + 36;
            q += 36 + ((size + 3) & ~3u);
        }
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
