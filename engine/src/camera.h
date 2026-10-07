#ifndef CAMERA_H
#define CAMERA_H

#include "vmath.h"

/* Camera areas (CARE chunk, tools/camera.py): which SMG camera is active where. */
typedef struct {
    unsigned char shape, zone; /* 0 box (centre origin), 1 cylinder (base origin), 2 sphere */
    unsigned short cam;        /* CAMS index, 0xFFFF if the stage has no such camera */
    float pos[3], rot[3], scale[3];
} CamArea; /* 40 bytes, matches tools/camera.py CARE */

/* Index of the last area (in the blob's priority order) that contains p, or -1 (AreaObjMgr::find_in). */
int camera_area_at(const CamArea *a, int n, V3 p);

#endif
