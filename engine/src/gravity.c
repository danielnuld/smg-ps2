#include "gravity.h"

#include <math.h>

/* One gravity's raw vector (direction * 4e6 / r^2), PlanetGravity::calcGravity. Returns 0 when out of range. */
static int one(const Gravity *g, const float p[3], float v[3])
{
    float dir[3], radius;
    if (g->kind == 0) { /* PointGravity::calcOwnGravityVector */
        float d[3] = {g->pos[0] - p[0], g->pos[1] - p[1], g->pos[2] - p[2]};
        float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float inv = len > 1e-4f ? 1.0f / len : 0.0f;
        dir[0] = d[0] * inv; dir[1] = d[1] * inv; dir[2] = d[2] * inv;
        if (g->range >= 0 && !(len < g->range + g->distant)) /* isInRangeDistance */
            return 0;
        radius = len;
    } else { /* ParallelGravity, RangeType_Sphere: in range if |plane pos - p| < range; scalar = base distance */
        if (g->range >= 0) {
            float d[3] = {g->pos[0] - p[0], g->pos[1] - p[1], g->pos[2] - p[2]};
            if (!(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < g->range * g->range))
                return 0;
        }
        dir[0] = -g->up[0]; dir[1] = -g->up[1]; dir[2] = -g->up[2];
        radius = 2000.0f; /* mBaseDistance default */
    }
    radius -= g->distant;
    if (radius < 1.0f)
        radius = 1.0f;
    float s = 4000000.0f / (radius * radius) * (g->inverse ? -1.0f : 1.0f);
    v[0] = dir[0] * s; v[1] = dir[1] * s; v[2] = dir[2] * s;
    return 1;
}

int gravity_at(const Gravity *g, int n, const float p[3], float out[3])
{
    /* PlanetGravityManager::calcTotalGravityVector: highest priority in range wins, equal priorities add up. */
    float total[3] = {0, 0, 0};
    int best = -1, any = 0;
    for (int i = 0; i < n; i++) {
        float v[3];
        if (g[i].priority < best || !one(&g[i], p, v))
            continue;
        if (g[i].priority > best) {
            best = g[i].priority;
            total[0] = v[0]; total[1] = v[1]; total[2] = v[2];
        } else {
            total[0] += v[0]; total[1] += v[1]; total[2] += v[2];
        }
        any = 1;
    }
    float l = sqrtf(total[0] * total[0] + total[1] * total[1] + total[2] * total[2]);
    float inv = l > 0 ? 1.0f / l : 0.0f;
    out[0] = total[0] * inv; out[1] = total[1] * inv; out[2] = total[2] * inv;
    return any;
}
