#include "lyt.h"

#include "mem.h"
#include "menu.h"
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* tools/lyt.py records (little endian) */
typedef struct { char name[24]; u16 npanes, nmats, nanims, ntex, tex0, nstr; u32 panes, mats, anims, strings; } LytHead;
typedef struct {
    char name[16];
    s16 parent, mat;
    u8 kind, flags, origin, alpha;
    float t[3], r[3], s[2], size[2];
    u8 vcol[4][4];
    float uv[8];
    float inflate[4];
    s16 frame_mat[4];
    u8 nframes, flip[4], pad[3];
    float font[2], space, line;
    u8 top[4], bottom[4];
    u16 str, len;
    u8 pos, align, font_idx, pad2;
} LytPane;
typedef struct { char name[20]; s16 tex[2]; u8 wrap[4]; float srt[5]; u8 c0[4], c1[4]; u8 flags, pad[3]; u16 tw, th; } LytMat;
typedef struct { char name[24]; u16 frames; u8 loop, pad; u16 ntracks, ntexmap; u32 tracks, texmap; } LytAnim;
typedef struct { s16 target; u8 is_mat, tag, index, sub, ctype, pad; u16 nkeys, pad2; u32 keys; } LytTrack;
enum { K_PAN, K_PIC, K_WND, K_TXT, K_BND };
enum { T_RLPA, T_RLVC, T_RLVI, T_RLMC, T_RLTS, T_RLTP };

#define LYT_CHUNKS 4
#define LYT_SLOTS 16
static struct { const u8 *c; u32 tex0; } chunks[LYT_CHUNKS];
static int nchunks;

/* a pane's live values, reset to the record each frame, then the animations */
typedef struct { float t[3], r[3], s[2], size[2]; float vc[4][4], alpha; u8 vis, lang_off; } PState; /* lang_off: another language's */
typedef struct { float srt[5], c0[4], c1[4]; s16 tex; } MState;
typedef struct { float m[6], alpha; u8 vis; } PWorld; /* 2x3: x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5 */

struct LytInst {
    const LytHead *h;
    u32 tex0; /* its chunk's first scene texture + the layout's tex0 */
    const LytPane *pane;
    const LytMat *mat;
    const LytAnim *anim;
    struct { const LytAnim *a; float frame, rate; s16 root; u8 ch, on, stopped; } slot[LYT_SLOTS];
    const u16 **text;
    u16 (*num)[12];  /* setTextBoxNumber's digits, per pane */
    s8 *vpos;        /* a text box's vertical position override, -1 none */
    const float **follow; /* setFollowPos per pane */
    u8 *follow_add;
    PState *ps;
    MState *ms;
    PWorld *pw;
    float ox, oy, alpha;
};

void lyt_init(const Scene *s)
{
    nchunks = 0;
    for (u32 i = 0; i < s->nlytr && nchunks < LYT_CHUNKS; i++) {
        chunks[nchunks].c = s->lytr[i];
        chunks[nchunks++].tex0 = s->lytr_tex0[i];
    }
}

static const LytAnim *find_anim(const LytInst *l, const char *name)
{
    for (int i = 0; i < l->h->nanims; i++)
        if (!strcasecmp(l->anim[i].name, name)) return &l->anim[i];
    return NULL;
}

int lyt_pane(const LytInst *l, const char *name)
{
    if (!l) return -1;
    for (int i = 0; i < l->h->npanes; i++)
        if (!strncmp(l->pane[i].name, name, 16)) return i;
    return -1;
}

static void reset_state(LytInst *l);

/* panes for one language (a name ending in its code: TxtConfirmJpJa) show for it alone; the plain one when the
 * language has none of its own. ponytail: the language is the build's (tools/msg.py LANG UsSpanish: UsSp) */
static void hide_other_languages(LytInst *l)
{
    static const char *codes[] = {"JpJa", "UsEn", "UsSp", "UsFr", "EuEn", "EuSp", "EuFr", "EuGe", "EuIt", "EuNl", "KrKr"};
    const char *cur = "UsSp";
    for (int i = 0; i < l->h->npanes; i++) {
        const char *n = l->pane[i].name;
        size_t len = strnlen(n, 16);
        if (len <= 4) continue;
        for (unsigned c = 0; c < sizeof codes / sizeof codes[0]; c++) {
            if (strncmp(n + len - 4, codes[c], 4)) continue;
            if (strcmp(codes[c], cur)) { l->ps[i].vis = 0; l->ps[i].lang_off = 1; break; }
            /* this language's own: the plain one hides (its name may be cut to 16: "ShaGalaxyNamUsSp" for
             * "ShaGalaxyName"): a sibling starting the same without a language code */
            for (int k = 0; k < l->h->npanes; k++) {
                const char *m = l->pane[k].name;
                size_t lk = strnlen(m, 16);
                if (k == i || l->pane[k].parent != l->pane[i].parent || lk < len - 4 || strncmp(m, n, len - 4)) continue;
                int coded = 0;
                for (unsigned d = 0; d < sizeof codes / sizeof codes[0] && lk > 4; d++) coded |= !strncmp(m + lk - 4, codes[d], 4);
                if (!coded) { l->ps[k].vis = 0; l->ps[k].lang_off = 1; }
            }
            break;
        }
    }
}

LytInst *lyt_new(const char *name)
{
    for (int c = 0; c < nchunks; c++) {
        const u8 *p = chunks[c].c;
        u32 n = *(const u32 *)p;
        for (u32 i = 0; i < n; i++) {
            const LytHead *h = (const LytHead *)(p + ((const u32 *)(p + 16))[i]);
            if (strcasecmp(h->name, name)) continue;
            LytInst *l = mem_alloc(MEM_SCENE, sizeof *l);
            l->h = h;
            l->tex0 = chunks[c].tex0 + h->tex0;
            l->pane = (const LytPane *)((const u8 *)h + h->panes);
            l->mat = (const LytMat *)((const u8 *)h + h->mats);
            l->anim = (const LytAnim *)((const u8 *)h + h->anims);
            l->text = mem_alloc(MEM_SCENE, h->npanes * sizeof *l->text);
            l->num = mem_alloc(MEM_SCENE, h->npanes * sizeof *l->num);
            l->vpos = mem_alloc(MEM_SCENE, h->npanes);
            l->follow = mem_alloc(MEM_SCENE, h->npanes * sizeof *l->follow);
            l->follow_add = mem_alloc(MEM_SCENE, h->npanes);
            memset(l->vpos, -1, h->npanes);
            l->ps = mem_alloc(MEM_SCENE, h->npanes * sizeof *l->ps);
            l->pw = mem_alloc(MEM_SCENE, h->npanes * sizeof *l->pw);
            l->ms = mem_alloc(MEM_SCENE, (h->nmats + 1) * sizeof *l->ms);
            for (int k = 0; k < h->npanes; k++) {
                const LytPane *p = &l->pane[k];
                if (p->kind == K_TXT && p->len) { /* its Layout_ message (LayoutManager::initTextBoxRecursive) */
                    char lb[64];
                    const u16 *st = (const u16 *)((const u8 *)h + h->strings) + p->str;
                    int n = p->len < 63 ? p->len : 63;
                    for (int j = 0; j < n; j++) lb[j] = (char)st[j];
                    lb[n] = 0;
                    l->text[k] = msg_get(lb);
                }
            }
            l->alpha = 1.0f;
            reset_state(l);
            hide_other_languages(l);
            return l;
        }
    }
    return NULL;
}

static int slot_for(LytInst *l, int root, int ch)
{
    int free_ = -1;
    for (int i = 0; i < LYT_SLOTS; i++) {
        if (l->slot[i].on && l->slot[i].root == root && l->slot[i].ch == ch) return i;
        if (!l->slot[i].on && free_ < 0) free_ = i;
    }
    return free_;
}

static int start(LytInst *l, int root, const char *anim, int ch)
{
    if (!l) return 0;
    const LytAnim *a = find_anim(l, anim);
    int k = a ? slot_for(l, root, ch) : -1;
    if (k < 0) return 0;
    l->slot[k].a = a; l->slot[k].frame = 0; l->slot[k].rate = 1.0f; l->slot[k].root = (s16)root; l->slot[k].ch = (u8)ch;
    l->slot[k].on = 1; l->slot[k].stopped = 0;
    return 1;
}

int lyt_start(LytInst *l, const char *anim, int ch) { return start(l, -1, anim, ch); }
int lyt_start_pane(LytInst *l, const char *pane, const char *anim, int ch)
{
    int p = lyt_pane(l, pane);
    return p >= 0 && start(l, p, anim, ch);
}

static int find_slot(const LytInst *l, int root, int ch)
{
    for (int i = 0; l && i < LYT_SLOTS; i++)
        if (l->slot[i].on && l->slot[i].root == root && l->slot[i].ch == ch) return i;
    return -1;
}
void lyt_set_frame(LytInst *l, int ch, float f) { int k = find_slot(l, -1, ch); if (k >= 0) { l->slot[k].frame = f; l->slot[k].stopped = 0; } }
void lyt_set_pane_frame(LytInst *l, const char *pane, int ch, float f)
{
    int k = find_slot(l, lyt_pane(l, pane), ch);
    if (k >= 0) { l->slot[k].frame = f; l->slot[k].stopped = 0; }
}
void lyt_set_rate(LytInst *l, int ch, float r) { int k = find_slot(l, -1, ch); if (k >= 0) l->slot[k].rate = r; }
void lyt_set_pane_rate(LytInst *l, const char *pane, int ch, float r)
{
    int k = find_slot(l, lyt_pane(l, pane), ch);
    if (k >= 0) l->slot[k].rate = r;
}
/* J3DFrameCtrl::isStop: at its end (a one-shot) or its rate 0 (setAnimFrameAndStop) */
int lyt_stopped(const LytInst *l, int ch) { int k = find_slot(l, -1, ch); return k < 0 || l->slot[k].stopped || l->slot[k].rate == 0; }
int lyt_pane_stopped(const LytInst *l, const char *pane, int ch)
{
    int k = find_slot(l, lyt_pane(l, pane), ch);
    return k < 0 || l->slot[k].stopped || l->slot[k].rate == 0;
}
float lyt_frame(const LytInst *l, int ch) { int k = find_slot(l, -1, ch); return k < 0 ? 0 : l->slot[k].frame; }

/* LayoutActor::movement: each animation a frame on; a looping one wraps, the others stop at their end */
void lyt_update(LytInst *l)
{
    if (!l) return;
    for (int i = 0; i < LYT_SLOTS; i++) {
        if (!l->slot[i].on || l->slot[i].stopped) continue;
        float end = l->slot[i].a->frames;
        l->slot[i].frame += l->slot[i].rate;
        if (l->slot[i].frame >= end) {
            if (l->slot[i].a->loop && end > 0) l->slot[i].frame = fmodf(l->slot[i].frame, end);
            else { l->slot[i].frame = end; l->slot[i].stopped = 1; }
        }
    }
}

void lyt_show_pane(LytInst *l, const char *pane, int show) { int p = lyt_pane(l, pane); if (p >= 0 && !l->ps[p].lang_off) l->ps[p].vis = show != 0; }
int lyt_pane_shown(const LytInst *l, const char *pane) { int p = lyt_pane(l, pane); return p >= 0 && l->ps[p].vis; }
void lyt_set_text(LytInst *l, const char *pane, const u16 *text) { int p = lyt_pane(l, pane); if (p >= 0) l->text[p] = text; }
void lyt_set_pos(LytInst *l, float x, float y) { if (l) { l->ox = x; l->oy = y; } }
void lyt_set_alpha(LytInst *l, float a) { if (l) l->alpha = a; }

/* nw4r lyt AnimTransformBasic: a step curve holds its last key at or before f; a Hermite one interpolates between
 * the keys round f with their slopes, holding the first / last value outside */
static float curve(const LytInst *l, const LytTrack *t, float f)
{
    const float (*k)[3] = (const float (*)[3])((const u8 *)l->h + t->keys);
    int n = t->nkeys;
    if (!n) return 0;
    if (f <= k[0][0] || n == 1) return k[0][1];
    if (f >= k[n - 1][0]) return k[n - 1][1];
    int i = 0;
    while (i + 2 < n && k[i + 1][0] <= f) i++;
    if (t->ctype != 2) return k[i][1];
    float d = k[i + 1][0] - k[i][0];
    if (d <= 0) return k[i + 1][1];
    float u = (f - k[i][0]) / d, u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * k[i][1] + (-2 * u3 + 3 * u2) * k[i + 1][1] + ((u3 - 2 * u2 + u) * k[i][2] + (u3 - u2) * k[i + 1][2]) * d;
}

static int under(const LytInst *l, int p, int root)
{
    for (; p >= 0; p = l->pane[p].parent)
        if (p == root) return 1;
    return 0;
}

/* the panes' and materials' values from their records (lyt_new): the animations write over them and what they
 * write stays (nw4r lyt: an AnimTransform sets the pane's members; another animation that has no track for one
 * leaves it as the last one left it) */
static void reset_state(LytInst *l)
{
    const LytHead *h = l->h;
    for (int i = 0; i < h->npanes; i++) {
        const LytPane *p = &l->pane[i];
        PState *s = &l->ps[i];
        memcpy(s->t, p->t, sizeof s->t); memcpy(s->r, p->r, sizeof s->r);
        memcpy(s->s, p->s, sizeof s->s); memcpy(s->size, p->size, sizeof s->size);
        for (int c = 0; c < 4; c++) for (int k = 0; k < 4; k++) s->vc[c][k] = p->vcol[c][k];
        s->alpha = p->alpha;
        s->vis = p->flags & 1;
    }
    for (int i = 0; i < h->nmats; i++) {
        const LytMat *m = &l->mat[i];
        MState *s = &l->ms[i];
        memcpy(s->srt, m->srt, sizeof s->srt);
        for (int k = 0; k < 4; k++) { s->c0[k] = m->c0[k]; s->c1[k] = m->c1[k]; }
        s->tex = m->tex[0];
    }
}

static void evaluate(LytInst *l)
{
    const LytHead *h = l->h;
    for (int k = 0; k < LYT_SLOTS; k++) {
        if (!l->slot[k].on) continue;
        const LytAnim *a = l->slot[k].a;
        const LytTrack *tr = (const LytTrack *)((const u8 *)h + a->tracks);
        const s16 *tmap = (const s16 *)((const u8 *)h + a->texmap);
        int root = l->slot[k].root;
        float f = l->slot[k].frame;
        for (int j = 0; j < a->ntracks; j++) {
            const LytTrack *t = &tr[j];
            if (t->is_mat) {
                if (t->target >= h->nmats) continue;
                if (root >= 0) { /* a pane's animation moves the materials of its panes only */
                    int used = 0;
                    for (int p = root; p < h->npanes && !used; p++) used = l->pane[p].mat == t->target && under(l, p, root);
                    if (!used) continue;
                }
                MState *m = &l->ms[t->target];
                float v = curve(l, t, f);
                if (t->tag == T_RLMC) {
                    if (t->sub >= 4 && t->sub < 8) m->c0[t->sub - 4] = v;
                    else if (t->sub >= 8 && t->sub < 12) m->c1[t->sub - 8] = v;
                } else if (t->tag == T_RLTS && t->index == 0 && t->sub < 5) m->srt[t->sub] = v;
                else if (t->tag == T_RLTP && t->index == 0) {
                    int ti = (int)v;
                    if (ti >= 0 && ti < a->ntexmap && tmap[ti] >= 0) m->tex = tmap[ti];
                }
                continue;
            }
            if (t->target >= h->npanes || (root >= 0 && !under(l, t->target, root))) continue;
            PState *s = &l->ps[t->target];
            float v = curve(l, t, f);
            switch (t->tag) {
            case T_RLPA:
                if (t->sub < 3) s->t[t->sub] = v;
                else if (t->sub < 6) s->r[t->sub - 3] = v;
                else if (t->sub < 8) s->s[t->sub - 6] = v;
                else if (t->sub < 10) s->size[t->sub - 8] = v;
                break;
            case T_RLVC:
                if (t->sub < 16) s->vc[t->sub / 4][t->sub % 4] = v;
                else if (t->sub == 16) s->alpha = v;
                break;
            case T_RLVI: if (!s->lang_off) s->vis = v != 0; break;
            }
        }
    }
    /* the tree, parents before children (file order): translation, z rotation, scale (180 degrees about x / y a
     * mirror); a pane's alpha its own times its parent's where the parent influences its children's */
    for (int i = 0; i < h->npanes; i++) {
        const PState *s = &l->ps[i];
        PWorld *w = &l->pw[i];
        float rz = s->r[2] * 0.017453293f, c = cosf(rz), sn = sinf(rz);
        float sx = s->s[0] * (fabsf(fabsf(s->r[1]) - 180.0f) < 1.0f ? -1.0f : 1.0f);
        float sy = s->s[1] * (fabsf(fabsf(s->r[0]) - 180.0f) < 1.0f ? -1.0f : 1.0f);
        float lm[6] = {c * sx, -sn * sy, s->t[0], sn * sx, c * sy, s->t[1]};
        int par = l->pane[i].parent;
        if (par < 0) {
            lm[2] += l->ox; lm[5] += l->oy;
            memcpy(w->m, lm, sizeof lm);
            w->alpha = s->alpha / 255.0f * l->alpha;
            w->vis = s->vis;
        } else {
            const PWorld *pw = &l->pw[par];
            const float *m = pw->m;
            w->m[0] = m[0] * lm[0] + m[1] * lm[3]; w->m[1] = m[0] * lm[1] + m[1] * lm[4]; w->m[2] = m[0] * lm[2] + m[1] * lm[5] + m[2];
            w->m[3] = m[3] * lm[0] + m[4] * lm[3]; w->m[4] = m[3] * lm[1] + m[4] * lm[4]; w->m[5] = m[3] * lm[2] + m[4] * lm[5] + m[5];
            w->alpha = s->alpha / 255.0f * ((l->pane[par].flags & 2) ? pw->alpha : 1.0f);
            w->vis = s->vis && pw->vis;
        }
        if (l->follow[i]) { /* LayoutPaneCtrl::reflectFollowPos: its global place (its children follow) */
            const float *f = l->follow[i];
            if (l->follow_add[i]) { w->m[2] += f[0]; w->m[5] -= f[1]; }
            else { w->m[2] = f[0] - 304.0f; w->m[5] = 228.0f - f[1]; }
        }
    }
}

#define LYT_KX (640.0f / 608.0f)
#define LYT_KY (448.0f / 456.0f)

/* a pane-local rectangle (x0, y0 top left .. x1, y1 bottom right, y up) through its matrix as two triangles; uv
 * of its corners (top left, top right, bottom left, bottom right) */
static void quad(const PWorld *w, float x0, float y0, float x1, float y1, const float uv[8], u16 tex, u32 rgba)
{
    float xy[4][2], u[4][2];
    const float px[4] = {x0, x1, x0, x1}, py[4] = {y0, y0, y1, y1};
    for (int k = 0; k < 4; k++) {
        float X = w->m[0] * px[k] + w->m[1] * py[k] + w->m[2], Y = w->m[3] * px[k] + w->m[4] * py[k] + w->m[5];
        xy[k][0] = 320.0f + X * LYT_KX;
        xy[k][1] = 224.0f - Y * LYT_KY;
        u[k][0] = uv[2 * k]; u[k][1] = uv[2 * k + 1];
    }
    static const u16 idx[6] = {0, 1, 2, 1, 3, 2};
    render_mesh2d((const float (*)[2])xy, (const float (*)[2])u, 4, idx, 2, tex, rgba, 0);
}

static u32 rgba_of(float r, float g, float b, float a) /* 0..255 each -> 0xAABBGGRR, 0x80 = 1.0 */
{
    int R = (int)(r * 128 / 255), G = (int)(g * 128 / 255), B = (int)(b * 128 / 255), A = (int)(a * 128 / 255);
    R = R < 0 ? 0 : R > 128 ? 128 : R; G = G < 0 ? 0 : G > 128 ? 128 : G;
    B = B < 0 ? 0 : B > 128 ? 128 : B; A = A < 0 ? 0 : A > 128 ? 128 : A;
    return (u32)R | (u32)G << 8 | (u32)B << 16 | (u32)A << 24;
}

/* a material's texture coordinates: the pane's through its texture SRT 0 about the centre (nw4r CalcTextureMtx); a
 * mirrored wrap was doubled into the texture (tex.to_ps2): half the scale */
static void mat_uv(const MState *m, const LytMat *mr, const float in[8], float out[8])
{
    float rr = m->srt[2] * 0.017453293f, c = cosf(rr), sn = sinf(rr);
    float hu = mr->wrap[0] == 2 ? 0.5f : 1.0f, hv = mr->wrap[1] == 2 ? 0.5f : 1.0f;
    for (int k = 0; k < 4; k++) {
        float u = in[2 * k] - 0.5f, v = in[2 * k + 1] - 0.5f;
        out[2 * k] = (0.5f + m->srt[0] + c * m->srt[3] * u - sn * m->srt[4] * v) * hu;
        out[2 * k + 1] = (0.5f + m->srt[1] + sn * m->srt[3] * u + c * m->srt[4] * v) * hv;
    }
}

/* a pane's colour: its vertex colours' mean, times its material's c1 where that is not baked in its texture */
static u32 pane_rgba(const LytInst *l, int i, float alpha)
{
    const PState *s = &l->ps[i];
    float c[4] = {0, 0, 0, 0};
    for (int v = 0; v < 4; v++) for (int k = 0; k < 4; k++) c[k] += s->vc[v][k] * 0.25f;
    int mi = l->pane[i].mat;
    if (mi >= 0 && mi < l->h->nmats && l->ms[mi].tex >= 0 && !(l->mat[mi].flags & 1))
        for (int k = 0; k < 4; k++) c[k] *= l->ms[mi].c1[k] / 255.0f;
    return rgba_of(c[0], c[1], c[2], c[3] * alpha);
}

static u16 tex_of(const LytInst *l, int mi) { return mi >= 0 && l->ms[mi].tex >= 0 ? (u16)(l->tex0 + l->ms[mi].tex) : 0xFFFF; }

static void draw_window(const LytInst *l, int i, const PWorld *w, float x0, float y0, float x1, float y1)
{
    const LytPane *p = &l->pane[i];
    u32 rgba = pane_rgba(l, i, w->alpha);
    int fm = p->nframes ? p->frame_mat[0] : -1;
    float cw = fm >= 0 ? l->mat[fm].tw : 0, ch = fm >= 0 ? l->mat[fm].th : 0;
    /* the content (its material) inside, inflated */
    int mi = p->mat;
    float cuv[8];
    if (mi >= 0 && mi < l->h->nmats) {
        mat_uv(&l->ms[mi], &l->mat[mi], p->uv, cuv);
        quad(w, x0 + cw - p->inflate[0], y0 - ch + p->inflate[2], x1 - cw + p->inflate[1], y1 + ch - p->inflate[3], cuv,
             tex_of(l, mi), rgba);
    }
    if (fm < 0 || fm >= l->h->nmats || l->ms[fm].tex < 0) return;
    u16 ft = tex_of(l, fm);
    u32 frgba = rgba_of(255, 255, 255, 255 * w->alpha);
    /* lyt::Window with one frame texture: the corners at its size, mirrored round; the edges stretch its inner
     * column / row */
    const float e = 0.98f;
    const float tl[8] = {0, 0, 1, 0, 0, 1, 1, 1}, tr[8] = {1, 0, 0, 0, 1, 1, 0, 1};
    const float bl[8] = {0, 1, 1, 1, 0, 0, 1, 0}, br[8] = {1, 1, 0, 1, 1, 0, 0, 0};
    const float top[8] = {e, 0, e, 0, e, 1, e, 1}, bot[8] = {e, 1, e, 1, e, 0, e, 0};
    const float lef[8] = {0, e, 1, e, 0, e, 1, e}, rig[8] = {1, e, 0, e, 1, e, 0, e};
    quad(w, x0, y0, x0 + cw, y0 - ch, tl, ft, frgba);
    quad(w, x1 - cw, y0, x1, y0 - ch, tr, ft, frgba);
    quad(w, x0, y1 + ch, x0 + cw, y1, bl, ft, frgba);
    quad(w, x1 - cw, y1 + ch, x1, y1, br, ft, frgba);
    quad(w, x0 + cw, y0, x1 - cw, y0 - ch, top, ft, frgba);
    quad(w, x0 + cw, y1 + ch, x1 - cw, y1, bot, ft, frgba);
    quad(w, x0, y0 - ch, x0 + cw, y1 + ch, lef, ft, frgba);
    quad(w, x1 - cw, y0 - ch, x1, y1 + ch, rig, ft, frgba);
}

static void draw_text(const LytInst *l, int i, const PWorld *w, float x0, float y0, float x1, float y1)
{
    const LytPane *p = &l->pane[i];
    const u16 *s = l->text[i];
    if (!s || !s[0]) return; /* no message: empty (initTextBoxPane without one) */
    float sx = sqrtf(w->m[0] * w->m[0] + w->m[3] * w->m[3]), sy = sqrtf(w->m[1] * w->m[1] + w->m[4] * w->m[4]);
    float fsx = p->font[0] * sx, fsy = p->font[1] * sy;
    float tw = game_text(s, p->font_idx, 0, 0, fsx, fsy, p->space * sx, 0, 0, 0);
    /* the box on the screen (axis-aligned: text boxes are not turned), the text placed in it by its position */
    float bx = 320.0f + (w->m[0] * x0 + w->m[2]) * LYT_KX, bw = (x1 - x0) * sx * LYT_KX;
    float by = 224.0f - (w->m[4] * y0 + w->m[5]) * LYT_KY, bh = (y0 - y1) * sy * LYT_KY;
    int hp = p->pos % 3, vp = l->vpos[i] >= 0 ? l->vpos[i] : p->pos / 3;
    float x = bx + (hp == 0 ? 0 : hp == 1 ? (bw - tw) / 2 : bw - tw);
    float lh = fsy * LYT_KY;
    float y = by + (vp == 0 ? 0 : vp == 1 ? (bh - lh) / 2 : bh - lh);
    const PState *ps = &l->ps[i];
    float r = 0, g = 0, b = 0, a = 0;
    for (int v = 0; v < 4; v++) { r += ps->vc[v][0]; g += ps->vc[v][1]; b += ps->vc[v][2]; a += ps->vc[v][3]; }
    /* the text colour: the box's top / bottom colours' mean, times the material's c1 (lyt TextBox) */
    float tc[3] = {(p->top[0] + p->bottom[0]) * 0.5f, (p->top[1] + p->bottom[1]) * 0.5f, (p->top[2] + p->bottom[2]) * 0.5f};
    float ta = (p->top[3] + p->bottom[3]) * 0.5f / 255.0f;
    int mi = p->mat;
    if (mi >= 0 && mi < l->h->nmats) for (int k = 0; k < 3; k++) tc[k] *= l->ms[mi].c1[k] / 255.0f;
    (void)r; (void)g; (void)b;
    u32 rgb = (u32)tc[0] << 16 | (u32)tc[1] << 8 | (u32)tc[2];
    game_text(s, p->font_idx, x, y, fsx, fsy, p->space * sx, rgb, w->alpha * ta * (a / 4.0f / 255.0f), 1);
}

float lyt_text_width(LytInst *l, const char *pane) /* getTextDrawRectRecursive's width: its text boxes' widest line */
{
    int r = lyt_pane(l, pane);
    float w = 0;
    for (int i = r; r >= 0 && i < l->h->npanes; i++) {
        const LytPane *p = &l->pane[i];
        if (p->kind != K_TXT || !under(l, i, r) || !l->text[i] || !l->text[i][0]) continue;
        float tw = game_text(l->text[i], p->font_idx, 0, 0, p->font[0], p->font[1], p->space, 0, 0, 0) / LYT_KX;
        if (tw > w) w = tw;
    }
    return w;
}

void lyt_draw(LytInst *l)
{
    if (!l) return;
    evaluate(l);
    for (int i = 0; i < l->h->npanes; i++) {
        const LytPane *p = &l->pane[i];
        const PWorld *w = &l->pw[i];
        if (!w->vis || w->alpha <= 0.004f || p->kind == K_PAN || p->kind == K_BND) continue;
        const PState *s = &l->ps[i];
        float wd = s->size[0], ht = s->size[1];
        int ox = p->origin % 3, oy = p->origin / 3;
        float x0 = -wd * ox / 2, y0 = ht * oy / 2, x1 = x0 + wd, y1 = y0 - ht;
        if (p->kind == K_PIC) {
            int mi = p->mat;
            float uv[8];
            if (mi >= 0 && mi < l->h->nmats) mat_uv(&l->ms[mi], &l->mat[mi], p->uv, uv);
            else memcpy(uv, p->uv, sizeof uv);
            quad(w, x0, y0, x1, y1, uv, tex_of(l, mi), pane_rgba(l, i, w->alpha));
        } else if (p->kind == K_WND) {
            draw_window(l, i, w, x0, y0, x1, y1);
        } else if (p->kind == K_TXT) {
            draw_text(l, i, w, x0, y0, x1, y1);
        }
    }
}

void lyt_show_rec(LytInst *l, const char *pane, int show)
{
    int r = lyt_pane(l, pane);
    for (int i = r; r >= 0 && i < l->h->npanes; i++)
        if (under(l, i, r) && !l->ps[i].lang_off) l->ps[i].vis = show != 0; /* another language's stays out */
}

void lyt_set_text_rec(LytInst *l, const char *pane, const u16 *text)
{
    int r = lyt_pane(l, pane);
    for (int i = r; r >= 0 && i < l->h->npanes; i++)
        if (l->pane[i].kind == K_TXT && under(l, i, r)) l->text[i] = text;
}

void lyt_set_number_rec(LytInst *l, const char *pane, int n)
{
    int r = lyt_pane(l, pane);
    for (int i = r; r >= 0 && i < l->h->npanes; i++) {
        if (l->pane[i].kind != K_TXT || !under(l, i, r)) continue;
        char d[12];
        int k = 0, v = n < 0 ? 0 : n;
        do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v && k < 11);
        for (int j = 0; j < k; j++) l->num[i][j] = (u16)d[k - 1 - j];
        l->num[i][k] = 0;
        l->text[i] = l->num[i];
    }
}

void lyt_set_vpos_rec(LytInst *l, const char *pane, int v)
{
    int r = lyt_pane(l, pane);
    for (int i = r; r >= 0 && i < l->h->npanes; i++)
        if (l->pane[i].kind == K_TXT && under(l, i, r)) l->vpos[i] = (s8)v;
}

void lyt_set_pane_frame_stop(LytInst *l, const char *pane, int ch, float frame)
{
    int k = find_slot(l, lyt_pane(l, pane), ch);
    if (k >= 0) { l->slot[k].frame = frame; l->slot[k].rate = 0; l->slot[k].stopped = 0; } /* setFrame, setRate(0) */
}

float lyt_pane_frame(const LytInst *l, const char *pane, int ch)
{
    int k = find_slot(l, lyt_pane(l, pane), ch);
    return k < 0 ? 0 : l->slot[k].frame;
}

float lyt_anim_frames(const LytInst *l, const char *anim)
{
    const LytAnim *a = l ? find_anim(l, anim) : NULL;
    return a ? a->frames : 0;
}

/* LayoutUtil isStarPointerPointingPane: the point inside the pane's rectangle (through its matrix's inverse) */
int lyt_pane_hit(LytInst *l, const char *pane, float x, float y)
{
    int i = lyt_pane(l, pane);
    if (i < 0) return 0;
    evaluate(l);
    const PWorld *w = &l->pw[i];
    if (!w->vis) return 0;
    const PState *s = &l->ps[i];
    float X = (x - 320.0f) / LYT_KX - w->m[2], Y = (224.0f - y) / LYT_KY - w->m[5];
    float det = w->m[0] * w->m[4] - w->m[1] * w->m[3];
    if (fabsf(det) < 1e-6f) return 0;
    float lx = (w->m[4] * X - w->m[1] * Y) / det, ly = (-w->m[3] * X + w->m[0] * Y) / det;
    const LytPane *p = &l->pane[i];
    int ox = p->origin % 3, oy = p->origin / 3;
    float x0 = -s->size[0] * ox / 2, y0 = s->size[1] * oy / 2;
    return lx >= x0 && lx <= x0 + s->size[0] && ly <= y0 && ly >= y0 - s->size[1];
}

void lyt_follow(LytInst *l, const char *pane, const float *pos, int add)
{
    int i = lyt_pane(l, pane);
    if (i >= 0) { l->follow[i] = pos; l->follow_add[i] = (u8)add; }
}

void lyt_copy_pane_trans(LytInst *l, const char *pane, float out[2])
{
    int i = lyt_pane(l, pane);
    out[0] = out[1] = 0;
    if (i < 0) return;
    evaluate(l);
    out[0] = l->pw[i].m[2] + 304.0f;
    out[1] = 228.0f - l->pw[i].m[5];
}

int lyt_pane_center(LytInst *l, const char *pane, float *x, float *y)
{
    int i = lyt_pane(l, pane);
    if (i < 0) return 0;
    evaluate(l);
    const PWorld *w = &l->pw[i];
    *x = 320.0f + w->m[2] * LYT_KX;
    *y = 224.0f - w->m[5] * LYT_KY;
    return 1;
}

