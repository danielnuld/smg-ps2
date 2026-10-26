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
/* Segment query (Petari KCollisionServer::checkArrow + KCHitArrow): the nearest prism the segment o + dir enters from
 * its front, *frac its place along dir (0, 1]; 0 if none */
int kcl_check_arrow(const unsigned char *kcl, const float o[3], const float dir[3], float *frac);

/* KCollisionServer::getPos (vertex 0..2), its edge normals, CollisionParts::calcCollidePosition (c moved in place) */
void kcl_prism_pos(const unsigned char *kcl, int prism, int v, float out[3]);
const float *kcl_edge_normal(const unsigned char *kcl, int prism, int e);
void kcl_collide_pos(const unsigned char *kcl, int prism, int flag, float c[3]);

#endif
