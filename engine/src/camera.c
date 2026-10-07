#include "camera.h"

/* Petari AreaForm.cpp: cube 45-54 + 104-125 (centre origin, 1000 x scale), cylinder 241-313 (base origin, radius
 * 500 x scale_x, height 500 x scale_y along R * Y), sphere 160-166 + 277-285 (radius 500 x scale_x). Rotation order
 * as every placement: R = Rz * Ry * Rx (degrees). */
static int inside(const CamArea *a, V3 p)
{
    V3 d = v3(p.x - a->pos[0], p.y - a->pos[1], p.z - a->pos[2]);
    if (a->shape == 2)
        return vlen(d) < 500.0f * a->scale[0];
    const float k = 3.14159265f / 180.0f;
    float cx = cosf(a->rot[0] * k), sx = sinf(a->rot[0] * k), cy = cosf(a->rot[1] * k), sy = sinf(a->rot[1] * k);
    float cz = cosf(a->rot[2] * k), sz = sinf(a->rot[2] * k);
    V3 x = v3(cz * cy, sz * cy, -sy);                                    /* columns of Rz * Ry * Rx */
    V3 y = v3(cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx);
    V3 z = v3(cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx);
    if (a->shape == 1) {
        float h = vdot(d, y);
        return h >= 0.0f && h <= 500.0f * a->scale[1] && vlen(vtangent(d, y)) < 500.0f * a->scale[0];
    }
    float lx = vdot(d, x), ly = vdot(d, y), lz = vdot(d, z);
    float hx = 500.0f * a->scale[0], hy = 500.0f * a->scale[1], hz = 500.0f * a->scale[2];
    return lx >= -hx && lx < hx && ly >= -hy && ly < hy && lz >= -hz && lz < hz;
}

int camera_area_at(const CamArea *a, int n, V3 p)
{
    for (int i = n - 1; i >= 0; i--)
        if (inside(&a[i], p))
            return i;
    return -1;
}
