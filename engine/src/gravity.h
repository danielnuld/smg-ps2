#ifndef GRAVITY_H
#define GRAVITY_H

/* Planet gravity as SMG computes it (Petari Game/Gravity: PlanetGravity.cpp, PointGravity.cpp, ParallelGravity.cpp,
 * CubeGravity.cpp, DiskGravity.cpp, SegmentGravity.cpp, DiskTorusGravity.cpp, ConeGravity.cpp, WireGravity.cpp,
 * PlanetGravityManager.cpp). The converter bakes each creator's matrix (tools/slice.py grav_record). A wire's points
 * follow the n records (p0 its first point, p1 how many). */
enum { GRAV_POINT, GRAV_PLANE, GRAV_PLANE_BOX, GRAV_PLANE_CYLINDER, GRAV_CUBE, GRAV_DISK, GRAV_SEGMENT, GRAV_TORUS, GRAV_CONE,
       GRAV_WIRE };

typedef struct {
    unsigned short kind, flags;
    int priority, inverse;
    float range, distant;      /* range < 0: unlimited */
    float m[4][3];             /* x, y, z axes and translation (meaning per kind: slice.py grav_record) */
    float p[4];
    int row;                   /* its OBJS row (switches) */
} Gravity;                     /* 88 bytes, matches slice.py GRAV */

/* GlobalGravityObj::updateSwitch: gravity_off[i] set leaves gravity i out (NULL: all on) */
extern const unsigned char *gravity_off;

/* Unit gravity direction at p (zero vector if no gravity reaches p or the pulls cancel). Returns 1 if it has one. */
int gravity_at(const Gravity *g, int n, const float p[3], float out[3]);
/* Forget the bounding spheres gravity_at keeps for the last gravity array (a new scene) */
void gravity_reset(void);

#endif
