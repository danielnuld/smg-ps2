#include "kcl.h"

#include <math.h>
#include <string.h>

typedef struct {
    unsigned pos, nrm, prism, octree;
    float thickness, min[3];
    int xmask, ymask, zmask, shift, xshift, xyshift;
} Header;

typedef struct {
    float height;
    unsigned short pos, nrm, edge[3], attr;
} Prism;

#define H(k) ((const Header *)(k))
#define V3(k, off, i) ((const float *)((k) + (off)) + 3 * (i))
#define PR(k, i) ((const Prism *)((k) + H(k)->prism) + (i))

const float *kcl_face_normal(const unsigned char *k, int i) { return V3(k, H(k)->nrm, PR(k, i)->nrm); }
int kcl_attribute(const unsigned char *k, int i) { return PR(k, i)->attr; }

/* KCollisionServer::searchBlock: walk the octree to the leaf holding (x, y, z); returns its u16 prism list. */
static const unsigned short *search_block(const unsigned char *k, int *shift, unsigned x, unsigned y, unsigned z)
{
    const Header *h = H(k);
    const unsigned char *node = k + h->octree;
    int s = *shift = h->shift;
    int off = (int)((((z >> s) << h->xyshift) | ((y >> s) << h->xshift) | (x >> s)) * 4);
    if (h->xyshift == -1 && h->xshift == -1)
        off = 0;
    while ((off = *(const int *)(node + off)) >= 0) {
        node += off;
        s = --(*shift);
        off = (int)(((((z >> s) & 1) << 2) | (((y >> s) & 1) << 1) | ((x >> s) & 1)) * 4);
    }
    return (const unsigned short *)(node + (off & 0x7FFFFFFF));
}

/* KCollisionServer::KCHitSphere, transcribed (param = 1, so threshold = thickness). */
static int hit_sphere(const unsigned char *k, const Prism *p, const float c[3], float radius, float *dist, unsigned char *flag)
{
    const Header *h = H(k);
    float rsq = radius * radius, threshold = h->thickness, d[4], nn = 0;
    const float *v0 = V3(k, h->pos, p->pos);
    float dir[3] = {c[0] - v0[0], c[1] - v0[1], c[2] - v0[2]};
    const float *n0 = V3(k, h->nrm, p->edge[0]), *n1 = V3(k, h->nrm, p->edge[1]), *n2 = V3(k, h->nrm, p->edge[2]);
    const float *fn = V3(k, h->nrm, p->nrm);
#define DOT(a, b) ((a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2])
    *flag = 0;
    if ((d[1] = DOT(dir, n0)) >= radius) return 0;
    if ((d[2] = DOT(dir, n1)) >= radius) return 0;
    if ((d[3] = DOT(dir, n2) - p->height) >= radius) return 0;
    d[0] = DOT(dir, fn);
    *dist = radius - d[0];
    if (*dist < 0) return 0;
    int region; /* 2-4 vertex, 5-7 edge */
    const float *ea, *eb;
    float da, db;
    if (d[1] > d[2] ? d[1] > d[3] : 0) { /* side0 */
        if (d[1] <= 0) goto face;
        if (d[2] > d[3]) { nn = DOT(n0, n1); region = nn * d[1] > d[2] ? 2 : 5; }
        else { nn = DOT(n0, n2); region = nn * d[1] > d[3] ? 2 : 7; }
    } else if (d[1] > d[2] ? 0 : d[2] > d[3]) { /* side1 */
        if (d[2] <= 0) goto face;
        if (d[3] > d[1]) { nn = DOT(n1, n2); region = nn * d[2] > d[3] ? 3 : 6; }
        else { nn = DOT(n1, n0); region = nn * d[2] > d[1] ? 3 : 5; }
    } else { /* side2 */
        if (d[3] <= 0) goto face;
        if (d[1] > d[2]) { nn = DOT(n2, n0); region = nn * d[3] > d[1] ? 4 : 7; }
        else { nn = DOT(n2, n1); region = nn * d[3] > d[2] ? 4 : 6; }
    }
    if (region <= 4) { /* vertex */
        float dv = d[region - 1];
        if (dv > d[0]) return 0;
        *dist = rsq - dv * dv;
        *flag = (unsigned char)region;
    } else { /* edge: closest point from two edge-plane distances */
        if (region == 5) { ea = n0; eb = n1; da = d[1]; db = d[2]; }
        else if (region == 6) { ea = n1; eb = n2; da = d[2]; db = d[3]; }
        else { ea = n2; eb = n0; da = d[3]; db = d[1]; }
        float t = (nn * db - da) / (nn * nn - 1.0f), s = db - t * nn;
        float q[3] = {t * ea[0] + s * eb[0], t * ea[1] + s * eb[1], t * ea[2] + s * eb[2]};
        float csq = DOT(q, q), cl = sqrtf(csq);
        if (cl > d[0] || cl >= radius) return 0;
        *dist = rsq - csq;
        *flag = (unsigned char)region;
    }
    *dist = sqrtf(*dist) - d[0];
    if (*dist < 0 || threshold < *dist) { *flag = 0; return 0; }
    return 1;
face:
    if (threshold < *dist) return 0;
    *flag = 1;
    return 1;
#undef DOT
}

int kcl_check_sphere(const unsigned char *k, const float c[3], float radius, KclHit *out, int max)
{
    const Header *h = H(k);
    /* outCheck: box to local integer space, clamped to the KCL's extent */
    int lo[3], hi[3], mask[3] = {h->xmask, h->ymask, h->zmask};
    for (int a = 0; a < 3; a++) {
        lo[a] = (int)(c[a] - radius - h->min[a]);
        hi[a] = (int)(c[a] + radius - h->min[a]);
        if (lo[a] < 0) lo[a] = 0;
        if (~mask[a] < hi[a]) hi[a] = ~mask[a];
        if (hi[a] < lo[a]) return 0;
    }
    int n = 0;
    /* the prisms tested this query (by id, a direct-mapped set: a prism in several cells of the walk is tested once;
     * two ids in one slot: a prism tested again, the same answer) */
    static unsigned short seen_p[256];
    static unsigned seen_q[256], qseq;
    if (++qseq == 0) qseq = 1;
    /* ponytail: plain cell walk with de-duplication instead of checkSphere's skip-list trick; same hits */
    for (unsigned z = lo[2]; (int)z <= hi[2];) {
        int zstep = 1 << 30;
        for (unsigned y = lo[1]; (int)y <= hi[1];) {
            int ystep = 1 << 30;
            for (unsigned x = lo[0]; (int)x <= hi[0];) {
                int shift;
                const unsigned short *list = search_block(k, &shift, x, y, z);
                int bs = 1 << shift, m = bs - 1;
                if (bs - (int)(y & m) < ystep) ystep = bs - (y & m);
                if (bs - (int)(z & m) < zstep) zstep = bs - (z & m);
                while (*++list) {
                    const Prism *p = PR(k, *list);
                    if (p->height <= 0) continue;
                    unsigned sl = *list & 255;
                    if (seen_q[sl] == qseq && seen_p[sl] == *list) continue;
                    seen_q[sl] = qseq; seen_p[sl] = *list;
                    int dup = 0;
                    for (int i = 0; i < n; i++) dup |= out[i].prism == *list;
                    if (dup || n >= max) continue;
                    float dist; unsigned char flag;
                    if (hit_sphere(k, p, c, radius, &dist, &flag)) {
                        out[n].prism = *list; out[n].flag = flag; out[n].dist = dist; n++;
                        if (n >= max) return n; /* the rest of the walk could add nothing */
                    }
                }
                x += bs - (x & m);
            }
            y += ystep;
        }
        z += zstep;
    }
    return n;
}

/* KCollisionServer::KCHitArrow, transcribed: the segment origin + dir (dir its whole length) through the prism's face
 * from its front (an origin on or behind the face does not hit); *frac in (0, 1] along dir */
static int hit_arrow(const unsigned char *k, const Prism *p, const float o[3], const float dir[3], float *frac)
{
    const Header *h = H(k);
    const float *v0 = V3(k, h->pos, p->pos), *fn = V3(k, h->nrm, p->nrm);
    float rel[3] = {o[0] - v0[0], o[1] - v0[1], o[2] - v0[2]};
#define DOT(a, b) ((a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2])
    float t = DOT(rel, fn);
    if (t <= 0.0f) return 0;
    float dd = DOT(fn, dir);
    if (0.0f < t + dd) return 0;
    t = t / -dd;
    float hit[3] = {dir[0] * t + rel[0], dir[1] * t + rel[1], dir[2] * t + rel[2]};
    if (0.01f < DOT(hit, V3(k, h->nrm, p->edge[0]))) return 0;
    if (0.01f < DOT(hit, V3(k, h->nrm, p->edge[1]))) return 0;
    if (0.01f + p->height < DOT(hit, V3(k, h->nrm, p->edge[2]))) return 0;
    *frac = t;
    return 1;
#undef DOT
}

int kcl_check_arrow(const unsigned char *k, const float o[3], const float dir[3], float *frac)
{
    const Header *h = H(k);
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-6f) return 0;
    float u[3] = {dir[0] / len, dir[1] / len, dir[2] / len}, p[3], t0 = 0.0f, t1 = len;
    int mask[3] = {h->xmask, h->ymask, h->zmask};
    /* the segment clipped to the octree's box (local space 0 .. ~mask) */
    for (int a = 0; a < 3; a++) {
        float s0 = o[a] - h->min[a], ext = (float)(~mask[a]) + 1.0f;
        if (fabsf(u[a]) < 1e-9f) { if (s0 < 0.0f || s0 >= ext) return 0; continue; }
        float ta = (0.0f - s0) / u[a], tb = (ext - s0) / u[a];
        if (ta > tb) { float q = ta; ta = tb; tb = q; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    if (t1 < t0) return 0;
    int found = 0;
    float best = 1.0f, t = t0;
    /* checkArrow: cell to cell along it; the first cell with a hit ends the walk, its nearest prism kept */
    for (int guard = 0; guard < 4096; guard++) {
        for (int a = 0; a < 3; a++) p[a] = o[a] - h->min[a] + u[a] * t;
        int c[3];
        for (int a = 0; a < 3; a++) {
            c[a] = (int)p[a];
            if (c[a] < 0 || c[a] > ~mask[a]) return 0;
        }
        int shift;
        const unsigned short *list = search_block(k, &shift, (unsigned)c[0], (unsigned)c[1], (unsigned)c[2]);
        while (*++list) {
            const Prism *pr = PR(k, *list);
            float f;
            if (pr->height > 0 && hit_arrow(k, pr, o, dir, &f) && (!found || f < best)) { best = f; found = *list; }
        }
        if (found) break;
        int bs = 1 << shift;
        float step = 1e30f;
        for (int a = 0; a < 3; a++) {
            if (fabsf(u[a]) < 1e-9f) continue;
            float lo = (float)(c[a] & ~(bs - 1)), d = u[a] > 0 ? (lo + bs - p[a]) / u[a] : (p[a] - lo) / -u[a];
            if (d < step) step = d;
        }
        if (t + step >= t1) break;
        t += step + 0.01f; /* (just into the next cell) */
    }
    if (found) *frac = best;
    return found;
}

/* KCollisionServer::getPos: a prism's triangle vertex 0..2 (calXvec: b x a) */
void kcl_prism_pos(const unsigned char *k, int i, int v, float out[3])
{
    const Header *h = H(k);
    const Prism *p = PR(k, i);
    const float *pos = V3(k, h->pos, p->pos), *e2 = V3(k, h->nrm, p->edge[2]);
    if (v == 0) { out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2]; return; }
    const float *a = v == 1 ? V3(k, h->nrm, p->edge[1]) : V3(k, h->nrm, p->nrm), *b = v == 1 ? V3(k, h->nrm, p->nrm) : V3(k, h->nrm, p->edge[0]);
    float x[3] = {a[2] * b[1] - a[1] * b[2], a[0] * b[2] - a[2] * b[0], a[1] * b[0] - a[0] * b[1]};
    float s = p->height / (x[0] * e2[0] + x[1] * e2[1] + x[2] * e2[2]);
    for (int j = 0; j < 3; j++) out[j] = pos[j] + s * x[j];
}
const float *kcl_edge_normal(const unsigned char *k, int i, int e) { return V3(k, H(k)->nrm, PR(k, i)->edge[e]); }

/* CollisionParts::calcCollidePosition: c onto the prism's face (flag 1), its edge 0 / 1 / 2 (2..4) or vertex 0..2 (5..7) */
void kcl_collide_pos(const unsigned char *k, int i, int flag, float c[3])
{
    if (flag >= 5) { kcl_prism_pos(k, i, flag - 5, c); return; }
    if (flag < 1) return;
    float o[3], d;
    const float *n = kcl_face_normal(k, i);
    kcl_prism_pos(k, i, 0, o);
    d = (c[0] - o[0]) * n[0] + (c[1] - o[1]) * n[1] + (c[2] - o[2]) * n[2];
    for (int j = 0; j < 3; j++) c[j] -= n[j] * d;
    if (flag == 1) return;
    n = kcl_edge_normal(k, i, flag - 2);
    if (flag == 4) kcl_prism_pos(k, i, 1, o);
    d = (c[0] - o[0]) * n[0] + (c[1] - o[1]) * n[1] + (c[2] - o[2]) * n[2];
    for (int j = 0; j < 3; j++) c[j] -= n[j] * d;
}
