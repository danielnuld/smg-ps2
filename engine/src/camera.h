#ifndef CAMERA_H
#define CAMERA_H

#include "vmath.h"

/* Camera areas (CARE chunk, tools/camera.py): which SMG camera is active where. */
typedef struct {
    unsigned char shape, zone; /* 0 box (centre origin), 1 cylinder (base origin), 2 sphere */
    unsigned short cam;        /* CAMS index, 0xFFFF if the stage has no such camera */
    float pos[3], rot[3], scale[3];
} CamArea; /* 40 bytes, matches tools/camera.py CARE */

/* One camera of cameraparam.bcam (CAMS chunk, tools/camera.py; defaults already applied). */
typedef struct {
    unsigned short zone;
    unsigned char kind, type; /* kind 'c' / 's' / 'o'; type: CAM_* below, 255 = not ported (runs as XZ_PARA) */
    unsigned short num, flags; /* flags bit i: noreset, nofovy, lofserpoff, antibluroff, collisionoff, subjectiveoff */
    float dist, angle_a, angle_b, wpoint[3], axis[3], up[3], woffset[3], loffset, loffsetv, fovy, upper, lower,
        uplay, lplay;
    int camint, num1, num2, vpanuse;
    float vpanaxis[3];
    unsigned char pad[4];
} CamParam; /* 128 bytes */

enum { CAM_XZ_PARA, CAM_TOWER, CAM_FOLLOW, CAM_WONDER_PLANET, CAM_EYEPOS_FIX };

/* What the camera follows (Mario): position, facing, up (against gravity), airborne. */
typedef struct { V3 pos, front, up; int jumping; } CamTarget;

/* Index of the last area (in the blob's priority order) that contains p, or -1 (AreaObjMgr::find_in). */
int camera_area_at(const CamArea *a, int n, V3 p);

/* Areas of other zones are ignored: the engine plays one zone in its local frame. */
void camera_init(const CamParam *cams, int ncams, const CamArea *areas, int nareas, int zone, const CamTarget *t);
void camera_update(const CamTarget *t);
void camera_pose(V3 *eye, V3 *watch, V3 *up, float *fovy);
/* Active camera (CAMS index, -1 = default) and its type, for the HUD. */
int camera_current(void);
const char *camera_type_name(void);

#endif
