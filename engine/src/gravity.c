#include "gravity.h"

#include <math.h>
#include <stdio.h>

static float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void sub(float o[3], const float a[3], const float b[3]) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static float len(const float a[3]) { return sqrtf(dot(a, a)); }
/* o = v / |v|, returns |v| (MR::separateScalarAndDirection) */
static float split(float o[3], const float v[3])
{
    float l = len(v), k = l > 1e-6f ? 1.0f / l : 0.0f;
    o[0] = v[0] * k; o[1] = v[1] * k; o[2] = v[2] * k;
    return l;
}
/* PlanetGravity::isInRangeDistance */
static int in_range(const Gravity *g, float d) { return g->range < 0 || d < g->range + g->distant; }

/* gravity from p towards point q: direction and distance */
static int towards(const Gravity *g, const float q[3], const float p[3], float dir[3], float *dist)
{
    float d[3];
    sub(d, q, p);
    *dist = split(dir, d);
    return in_range(g, *dist);
}

/* CubeGravity: calcGravityArea (per axis 0 below, 1 between, 2 above; inactive faces cut the region off), then
 * calcFaceGravity / calcEdgeGravity / calcCornerGravity */
static int cube(const Gravity *g, const float p[3], float dir[3], float *dist)
{
    float rel[3], u[3][3], l[3];
    int r[3];
    sub(rel, p, g->m[3]);
    for (int k = 0; k < 3; k++) {
        l[k] = split(u[k], g->m[k]);
        float x = dot(rel, u[k]);
        r[k] = x < -l[k] ? 0 : x <= l[k] ? 1 : 2;
        if ((r[k] == 0 && !(g->flags & 2 << 2 * k)) || (r[k] == 2 && !(g->flags & 1 << 2 * k)))
            return 0;
    }
    int outside = (r[0] != 1) + (r[1] != 1) + (r[2] != 1);
    if (outside == 0)
        return 0;
    float q[3] = {g->m[3][0], g->m[3][1], g->m[3][2]};
    if (outside == 1) { /* face: straight at the face, height above it */
        int k = r[0] != 1 ? 0 : r[1] != 1 ? 1 : 2;
        float s = r[k] == 0 ? 1.0f : -1.0f, d[3];
        dir[0] = u[k][0] * s; dir[1] = u[k][1] * s; dir[2] = u[k][2] * s;
        sub(d, q, p);
        *dist = dot(dir, d) - l[k];
        if (*dist < 0) *dist = 0;
        return in_range(g, *dist);
    }
    /* edge (two axes outside: nearest point on the edge line) or corner (three: the vertex) */
    int along = -1;
    for (int k = 0; k < 3; k++) {
        if (r[k] == 1) { along = k; continue; }
        float s = r[k] == 0 ? -1.0f : 1.0f;
        q[0] += g->m[k][0] * s; q[1] += g->m[k][1] * s; q[2] += g->m[k][2] * s;
    }
    float d[3];
    sub(d, q, p);
    if (along >= 0) {
        float t = dot(d, u[along]);
        d[0] -= u[along][0] * t; d[1] -= u[along][1] * t; d[2] -= u[along][2] * t;
    }
    if (len(d) < 1e-6f) { /* exactly on it: Petari takes centre -> edge / vertex, distance 0 */
        sub(d, q, g->m[3]);
        split(dir, d);
        *dist = 0;
        return in_range(g, 0);
    }
    *dist = split(dir, d);
    return in_range(g, *dist);
}

/* MR::calcPerpendicFootToLineInside: the point of segment a-b nearest p */
static void foot(float o[3], const float p[3], const float a[3], const float b[3])
{
    float ab[3], ap[3];
    sub(ab, b, a); sub(ap, p, a);
    float t = dot(ap, ab) / dot(ab, ab);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    o[0] = a[0] + ab[0] * t; o[1] = a[1] + ab[1] * t; o[2] = a[2] + ab[2] * t;
}

/* ConeGravity::calcOwnGravityVector: m = base axes (x radius, y up to the apex), m[3] the base centre, p0 the top cut
 * rate, flags bit 0 the bottom on */
static int cone(const Gravity *g, const float p[3], float dir[3], float *dist)
{
    const float *ax = g->m[1], *base = g->m[3];
    float u[3], rel[3], pb[3], q[3], d[3];
    float h = split(u, ax), R = len(g->m[0]), cut = g->p[0];
    sub(rel, p, base);
    float y = dot(rel, u);
    pb[0] = rel[0] - u[0] * y; pb[1] = rel[1] - u[1] * y; pb[2] = rel[2] - u[2] * y;
    if (dot(pb, pb) < 1e-6f) { /* on the axis */
        *dist = fabsf(y);
        if (y > 0 && (*dist -= h * (1.0f - cut)) < 0) *dist = 0;
        if (!in_range(g, *dist)) return 0;
        float s = y > 0 ? -1.0f : 1.0f;
        dir[0] = u[0] * s; dir[1] = u[1] * s; dir[2] = u[2] * s;
        return 1;
    }
    float r = len(pb);
    int inside = h > 1e-3f && R > 1e-3f && r < R - y * (R / h);
    float rim[3] = {base[0] + pb[0] * R / r, base[1] + pb[1] * R / r, base[2] + pb[2] * R / r};
    float apex[3] = {base[0] + ax[0], base[1] + ax[1], base[2] + ax[2]};
    if (y < 0) { /* under the base */
        if (!(g->flags & 1)) return 0;
        foot(q, p, base, rim);
        sub(d, q, p);
        if (dot(d, d) < 1e-6f) { dir[0] = -u[0]; dir[1] = -u[1]; dir[2] = -u[2]; *dist = 0; return 1; }
        return towards(g, q, p, dir, dist);
    }
    if (cut < 0.01f)
        foot(q, p, rim, apex);
    else {
        float top[3], fb[3], a[3], b[3];
        for (int k = 0; k < 3; k++) {
            top[k] = apex[k] * (1.0f - cut) + rim[k] * cut;
            fb[k] = base[k] + ax[k] * (1.0f - cut);
        }
        sub(a, p, top); sub(b, top, fb);
        if (dot(a, b) <= 0) { /* over the cut top: straight down onto it */
            sub(a, p, fb);
            *dist = dot(u, a);
            if (*dist < 0) *dist = 0;
            if (!in_range(g, *dist)) return 0;
            dir[0] = -u[0]; dir[1] = -u[1]; dir[2] = -u[2];
            return 1;
        }
        foot(q, p, rim, top);
    }
    sub(d, q, p);
    if (dot(d, d) < 1e-6f) { /* on the surface: -pb without its part along the generatrix */
        float gd[3], e[3];
        sub(e, apex, rim);
        split(gd, e);
        float t = -dot(pb, gd);
        e[0] = -pb[0] - gd[0] * t; e[1] = -pb[1] - gd[1] * t; e[2] = -pb[2] - gd[2] * t;
        if (dot(e, e) < 1e-6f) { dir[0] = -u[0]; dir[1] = -u[1]; dir[2] = -u[2]; }
        else split(dir, e);
        *dist = 0;
        return 1;
    }
    if (inside) { /* inside the cone: pushed out, from q (as Petari, no range test) */
        float o[3];
        sub(o, p, q);
        split(dir, o);
        *dist = 0;
        return 1;
    }
    return towards(g, q, p, dir, dist);
}

static const float *wire_pts; /* the wires' points, after the records */
const unsigned char *gravity_off;

/* One gravity's raw vector (direction * 4e6 / r^2), PlanetGravity::calcGravity. Returns 0 when out of range. */
static int one(const Gravity *g, const float p[3], float v[3])
{
    float dir[3], radius, rel[3], up[3];
    switch (g->kind) {
    case GRAV_POINT: /* PointGravity::calcOwnGravityVector */
        if (!towards(g, g->m[3], p, dir, &radius)) return 0;
        break;
    case GRAV_PLANE: /* ParallelGravity, RangeType_Sphere: in range if |plane pos - p| < range */
        if (g->range >= 0) {
            sub(rel, g->m[3], p);
            if (!(dot(rel, rel) < g->range * g->range)) return 0;
        }
        split(up, g->m[1]);
        dir[0] = -up[0]; dir[1] = -up[1]; dir[2] = -up[2];
        radius = g->p[0];
        break;
    case GRAV_PLANE_BOX: { /* ParallelGravity::isInBoxRange: |rel . axis| <= |axis|^2 per (scaled) axis */
        float d[3];
        sub(rel, p, g->m[3]);
        for (int k = 0; k < 3; k++) {
            float e = dot(g->m[k], g->m[k]);
            d[k] = dot(rel, g->m[k]);
            if (d[k] < -e || e < d[k]) return 0;
        }
        radius = g->p[0];
        if (g->flags < 3) radius += fabsf(d[g->flags]) / len(g->m[g->flags]);
        split(up, g->m[1]);
        dir[0] = -up[0]; dir[1] = -up[1]; dir[2] = -up[2];
        break;
    }
    case GRAV_PLANE_CYLINDER: { /* ParallelGravity::isInCylinderRange */
        split(up, g->m[1]);
        sub(rel, p, g->m[3]);
        float h = dot(up, rel);
        if (h < 0 || g->p[2] < h) return 0;
        rel[0] -= up[0] * h; rel[1] -= up[1] * h; rel[2] -= up[2] * h;
        float r = len(rel);
        if (r > g->p[1]) return 0;
        radius = g->p[0] + r;
        dir[0] = -up[0]; dir[1] = -up[1]; dir[2] = -up[2];
        break;
    }
    case GRAV_CUBE:
        if (!cube(g, p, dir, &radius)) return 0;
        break;
    case GRAV_DISK: { /* DiskGravity::calcOwnGravityVector */
        const float *n = g->m[1];
        sub(rel, p, g->m[3]);
        float y = dot(rel, n), side[3] = {rel[0] - n[0] * y, rel[1] - n[1] * y, rel[2] - n[2] * y}, ds[3];
        if (!(g->flags & 1) && y < 0) return 0;
        float r = split(ds, side);
        if (g->p[1] > -1.0f && dot(ds, g->m[0]) < g->p[1]) return 0;
        if (r <= g->p[0]) {
            float s = y >= 0 ? -1.0f : 1.0f;
            dir[0] = n[0] * s; dir[1] = n[1] * s; dir[2] = n[2] * s;
            radius = fabsf(y);
            if (!in_range(g, radius)) return 0;
        } else {
            if (!(g->flags & 2)) return 0;
            float q[3] = {g->m[3][0] + ds[0] * g->p[0], g->m[3][1] + ds[1] * g->p[0], g->m[3][2] + ds[2] * g->p[0]};
            if (!towards(g, q, p, dir, &radius)) return 0;
        }
        break;
    }
    case GRAV_SEGMENT: { /* SegmentGravity::calcOwnGravityVector (hemisphere extensions are always 0) */
        const float *ax = g->m[1];
        sub(rel, p, g->m[3]);
        float y = dot(rel, ax);
        if (g->p[1] > -1.0f) {
            float b[3] = {rel[0] - ax[0] * y, rel[1] - ax[1] * y, rel[2] - ax[2] * y}, bd[3];
            split(bd, b);
            if (dot(bd, g->m[0]) < g->p[1]) return 0;
        }
        float t = y;
        if (y < 0) { if (!(g->flags & 1)) return 0; t = 0; }
        else if (y > g->p[0]) { if (!(g->flags & 2)) return 0; t = g->p[0]; }
        float q[3] = {g->m[3][0] + ax[0] * t, g->m[3][1] + ax[1] * t, g->m[3][2] + ax[2] * t};
        if (!towards(g, q, p, dir, &radius)) return 0;
        break;
    }
    case GRAV_TORUS: { /* DiskTorusGravity::calcOwnGravityVector: m[1] the axis, p0 radius, p1 disk radius, flags bit 0
                        * both sides, bits 1-2 the edge type (0 no edge, 1 inner only, 2 outer only, 3 both) */
        const float *ax = g->m[1];
        int edge = g->flags >> 1 & 3;
        sub(rel, p, g->m[3]);
        float y = dot(rel, ax), side[3] = {rel[0] - ax[0] * y, rel[1] - ax[1] * y, rel[2] - ax[2] * y}, ds[3];
        if (!(g->flags & 1) && y < 0) return 0;
        float r = split(ds, side), inner = g->p[0] - g->p[1], edge_r;
        if (r < 1e-3f) { /* MR::makeAxisVerticalZX: any direction in the plane */
            float z[3] = {0, 0, 1}, t = dot(z, ax);
            float w[3] = {z[0] - ax[0] * t, z[1] - ax[1] * t, z[2] - ax[2] * t};
            if (dot(w, w) < 1e-6f) { w[0] = 1; w[1] = 0; w[2] = 0; }
            split(ds, w);
        }
        if (r < inner) {
            if (edge == 0 || edge == 2) return 0;
            edge_r = inner;
        } else if (r > g->p[0]) {
            if (edge == 0 || edge == 1) return 0;
            edge_r = g->p[0];
        } else {
            float s = y >= 0 ? -1.0f : 1.0f;
            dir[0] = ax[0] * s; dir[1] = ax[1] * s; dir[2] = ax[2] * s;
            radius = fabsf(y);
            if (!in_range(g, radius)) return 0;
            break;
        }
        float q[3] = {g->m[3][0] + ds[0] * edge_r, g->m[3][1] + ds[1] * edge_r, g->m[3][2] + ds[2] * edge_r};
        if (!towards(g, q, p, dir, &radius)) return 0;
        break;
    }
    case GRAV_CONE:
        if (!cone(g, p, dir, &radius)) return 0;
        break;
    case GRAV_WIRE: { /* WireGravity::calcOwnGravityVector: the nearest point of its polyline */
        const float *w = wire_pts + 3 * (int)g->p[0];
        float best = -1.0f, q[3], d[3];
        for (int i = 0; i + 1 < (int)g->p[1]; i++) {
            float f[3];
            foot(f, p, w + 3 * i, w + 3 * i + 3);
            sub(d, f, p);
            if (dot(d, d) < best || best < 0) { best = dot(d, d); q[0] = f[0]; q[1] = f[1]; q[2] = f[2]; }
        }
        if (best < 0 || !towards(g, q, p, dir, &radius)) return 0;
        break;
    }
    default:
        return 0;
    }
    radius -= g->distant;
    if (radius < 1.0f)
        radius = 1.0f;
    float s = 4000000.0f / (radius * radius) * (g->inverse ? -1.0f : 1.0f);
    v[0] = dir[0] * s; v[1] = dir[1] * s; v[2] = dir[2] * s;
    return 1;
}

/* A sphere outside which a gravity is never in range (in_range: distance < range + distant from its point, cube,
 * disk rim or segment), so most gravities of a galaxy are passed over with one distance test. r2 < 0: no bound
 * (unlimited range) */
#define MAX_BOUNDS 2048
static const Gravity *bound_of;
static int bound_n;
static float bound_c[MAX_BOUNDS][3], bound_r2[MAX_BOUNDS];

void gravity_reset(void) { bound_of = 0; bound_n = 0; }

static void bounds(const Gravity *g, int n)
{
    bound_of = g; bound_n = n;
    wire_pts = (const float *)(g + n);
    for (int i = 0; i < n && i < MAX_BOUNDS; i++) {
        const Gravity *q = &g[i];
        float R = q->range + q->distant, r = -1.0f;
        bound_c[i][0] = q->m[3][0]; bound_c[i][1] = q->m[3][1]; bound_c[i][2] = q->m[3][2];
        if (q->range >= 0) {
            if (q->kind == GRAV_POINT) r = R;
            else if (q->kind == GRAV_CUBE) r = R + sqrtf(dot(q->m[0], q->m[0]) + dot(q->m[1], q->m[1]) + dot(q->m[2], q->m[2]));
            else if (q->kind == GRAV_DISK || q->kind == GRAV_TORUS) r = R + q->p[0];
            else if (q->kind == GRAV_CONE) r = R + len(q->m[0]) + len(q->m[1]);
            else if (q->kind == GRAV_WIRE) { /* around its first point, out to its farthest */
                const float *w = (const float *)(g + n) + 3 * (int)q->p[0];
                float far = 0, d[3];
                for (int k = 0; k < (int)q->p[1]; k++) { sub(d, w + 3 * k, w); if (dot(d, d) > far) far = dot(d, d); }
                bound_c[i][0] = w[0]; bound_c[i][1] = w[1]; bound_c[i][2] = w[2];
                r = R + sqrtf(far);
            }
            else if (q->kind == GRAV_SEGMENT) {
                for (int k = 0; k < 3; k++) bound_c[i][k] += q->m[1][k] * q->p[0] * 0.5f;
                r = R + q->p[0] * 0.5f;
            }
        }
        /* the plane kinds: their own range (a sphere, a box of orthogonal axes, a cylinder), a hair over (rounding) */
        float s = -1.0f;
        if (q->kind == GRAV_PLANE && q->range >= 0) s = q->range;
        else if (q->kind == GRAV_PLANE_BOX) {
            float a = dot(q->m[0], q->m[0]), b = dot(q->m[1], q->m[1]), c = dot(q->m[2], q->m[2]);
            float ab = dot(q->m[0], q->m[1]), bc = dot(q->m[1], q->m[2]), ca = dot(q->m[2], q->m[0]);
            if (ab * ab <= 1e-8f * a * b && bc * bc <= 1e-8f * b * c && ca * ca <= 1e-8f * c * a) s = sqrtf(a + b + c);
        } else if (q->kind == GRAV_PLANE_CYLINDER && q->p[1] >= 0 && q->p[2] >= 0) {
            float up[3];
            split(up, q->m[1]);
            for (int k = 0; k < 3; k++) bound_c[i][k] += up[k] * q->p[2] * 0.5f;
            s = sqrtf(q->p[2] * q->p[2] * 0.25f + q->p[1] * q->p[1]);
        }
        if (s >= 0) r = s * 1.001f + 10.0f;
        bound_r2[i] = r < 0 ? -1.0f : r * r;
    }
}

/* The bounds in a hashed grid of GCELL cubes: each bucket lists, ascending, the bounded gravities whose sphere's box
 * meets a cell hashed there; the unbounded ones (and spheres over GMAXC cells) are listed apart, for every point.
 * gravity_at walks the merge of the two lists in index order: the same gravities in the same order as a walk over
 * all of them that passes over those whose sphere misses p (a point in a sphere is in its box, in one of its cells).
 * The pool full: no grid, the walk over all. */
#define GCELL 8192.0f
#define GBUCKETS 256
#define GMAXC 125
#define GPOOL 16384
static unsigned short g_first[GBUCKETS + 1], g_pool[GPOOL], g_always[MAX_BOUNDS];
static int g_nalways, g_on;
static int cell_of(float x) { return (int)floorf(x / GCELL); }
static unsigned bucket_of(int x, int y, int z) { return ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u) % GBUCKETS; }
static int sphere_cells(int i, int lo[3], int hi[3]) /* its box's cells, 0 if unbounded or over GMAXC */
{
    if (bound_r2[i] < 0) return 0;
    float r = sqrtf(bound_r2[i]) + 1.0f; /* (a hair over: rounding) */
    long cells = 1;
    for (int k = 0; k < 3; k++) {
        float a = bound_c[i][k] - r, b = bound_c[i][k] + r;
        if (!(a > -1e9f && b < 1e9f)) return 0;
        lo[k] = cell_of(a); hi[k] = cell_of(b);
        cells *= hi[k] - lo[k] + 1;
    }
    return cells <= GMAXC;
}
static void grid_build(int n)
{
    static unsigned short cnt[GBUCKETS], last[GBUCKETS];
    g_on = 0; g_nalways = 0;
    if (n > MAX_BOUNDS) return;
    for (int pass = 0; pass < 2; pass++) { /* count, then fill */
        for (int b = 0; b < GBUCKETS; b++) { cnt[b] = 0; last[b] = 0xFFFF; }
        g_nalways = 0;
        for (int i = 0; i < n; i++) {
            int lo[3], hi[3];
            if (!sphere_cells(i, lo, hi)) { g_always[g_nalways++] = (unsigned short)i; continue; }
            for (int x = lo[0]; x <= hi[0]; x++)
                for (int y = lo[1]; y <= hi[1]; y++)
                    for (int z = lo[2]; z <= hi[2]; z++) {
                        unsigned b = bucket_of(x, y, z);
                        if (last[b] == i) continue; /* (two of its cells in one bucket) */
                        last[b] = (unsigned short)i;
                        if (pass) g_pool[g_first[b] + cnt[b]] = (unsigned short)i;
                        cnt[b]++;
                    }
        }
        if (!pass) {
            unsigned t = 0;
            for (int b = 0; b < GBUCKETS; b++) { g_first[b] = (unsigned short)t; t += cnt[b]; if (t > GPOOL) return; }
            g_first[GBUCKETS] = (unsigned short)t;
        }
    }
    g_on = 1;
    printf("gravity grid: %d gravities, %d for every point, %u in buckets\n", n, g_nalways, (unsigned)g_first[GBUCKETS]);
}

static inline void visit(const Gravity *g, int i, const float p[3], float total[3], int *best, int *any)
{
    float v[3];
    if (g[i].priority < *best || (gravity_off && gravity_off[i]))
        return;
    if (i < MAX_BOUNDS && bound_r2[i] >= 0) {
        float d[3];
        sub(d, p, bound_c[i]);
        if (dot(d, d) >= bound_r2[i])
            return;
    }
    if (!one(&g[i], p, v))
        return;
    if (g[i].priority > *best) {
        *best = g[i].priority;
        total[0] = v[0]; total[1] = v[1]; total[2] = v[2];
    } else {
        total[0] += v[0]; total[1] += v[1]; total[2] += v[2];
    }
    *any = 1;
}

int gravity_at(const Gravity *g, int n, const float p[3], float out[3])
{
    /* PlanetGravityManager::calcTotalGravityVector: highest priority in range wins, equal priorities add up. */
    float total[3] = {0, 0, 0};
    int best = -1, any = 0;
    if (g != bound_of || n != bound_n) {
        bounds(g, n);
        grid_build(n);
    }
    if (g_on && p[0] > -1e9f && p[0] < 1e9f && p[1] > -1e9f && p[1] < 1e9f && p[2] > -1e9f && p[2] < 1e9f) {
        unsigned b = bucket_of(cell_of(p[0]), cell_of(p[1]), cell_of(p[2]));
        const unsigned short *a = g_always, *ae = g_always + g_nalways, *c = g_pool + g_first[b], *ce = g_pool + g_first[b + 1];
        while (a < ae || c < ce) /* the two lists merged, in index order */
            visit(g, c >= ce || (a < ae && *a < *c) ? *a++ : *c++, p, total, &best, &any);
    } else
        for (int i = 0; i < n; i++)
            visit(g, i, p, total, &best, &any);
    float l = sqrtf(total[0] * total[0] + total[1] * total[1] + total[2] * total[2]);
    float inv = l > 0 ? 1.0f / l : 0.0f;
    out[0] = total[0] * inv; out[1] = total[1] * inv; out[2] = total[2] * inv;
    return any && l > 0; /* pulls that cancel: no direction (MarioActor::updateGravityVec keeps the last) */
}
