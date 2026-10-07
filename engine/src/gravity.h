#ifndef GRAVITY_H
#define GRAVITY_H

/* Planet gravity as SMG computes it (Petari Game/Gravity: PlanetGravity.cpp, PointGravity.cpp, ParallelGravity.cpp,
 * CubeGravity.cpp, DiskGravity.cpp, SegmentGravity.cpp, PlanetGravityManager.cpp). The converter bakes each
 * creator's matrix (tools/slice.py grav_record); kinds without an evaluator (wire, torus, cone) are not written. */
enum { GRAV_POINT, GRAV_PLANE, GRAV_PLANE_BOX, GRAV_PLANE_CYLINDER, GRAV_CUBE, GRAV_DISK, GRAV_SEGMENT };

typedef struct {
    unsigned short kind, flags;
    int priority, inverse;
    float range, distant;      /* range < 0: unlimited */
    float m[4][3];             /* x, y, z axes and translation (meaning per kind: slice.py grav_record) */
    float p[4];
} Gravity;                     /* 84 bytes, matches slice.py GRAV */

/* Unit gravity direction at p (zero vector if no gravity reaches p). Returns 1 if any gravity applied. */
int gravity_at(const Gravity *g, int n, const float p[3], float out[3]);

#endif
