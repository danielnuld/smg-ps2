#ifndef KCL_H
#define KCL_H

/* KCL collision queries on the little-endian KCL that tools/slice.py writes (layout: Petari KCollision.hpp:9-41). */
typedef struct {
    unsigned short prism; /* 1-based prism index */
    unsigned char flag;   /* KCHitSphere's flag: 1 face, 2-4 vertex, 5-7 edge */
    float dist;           /* penetration depth */
} KclHit;

/* Sphere query in the KCL's object space (Petari KCollisionServer::checkSphere + KCHitSphere, param = 1).
 * Returns the number of hits written to out (at most max). */
int kcl_check_sphere(const unsigned char *kcl, const float c[3], float radius, KclHit *out, int max);
/* Face normal of a prism (unit), for pushing out. */
const float *kcl_face_normal(const unsigned char *kcl, int prism);
int kcl_attribute(const unsigned char *kcl, int prism);

#endif
