#ifndef GRAVITY_H
#define GRAVITY_H

/* Planet gravity as SMG computes it (Petari Game/Gravity/PlanetGravity.cpp, PointGravity.cpp, ParallelGravity.cpp,
 * PlanetGravityManager.cpp). Only the two kinds the slice places: point and plane (sphere range). */
typedef struct {
    unsigned short kind, zone; /* 0 point, 1 plane */
    int priority, inverse;
    float range, distant;      /* range < 0: unlimited */
    float pos[3], up[3];       /* point: centre; plane: a point on the plane and its normal */
} Gravity;                     /* 44 bytes, matches slice.py GRAV */

/* Unit gravity direction at p (zero vector if no gravity reaches p). Returns 1 if any gravity applied. */
int gravity_at(const Gravity *g, int n, const float p[3], float out[3]);

#endif
