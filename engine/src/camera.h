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
    int evfrm;          /* an event camera's frames (getEventCameraFrames), -1 for good */
    unsigned evclass;   /* an event camera's class (camera.py class_hash), 0 otherwise; num = CameraSetId << 4 | NN */
} CamParam; /* 132 bytes */

enum { CAM_XZ_PARA, CAM_TOWER, CAM_FOLLOW, CAM_WONDER_PLANET, CAM_EYEPOS_FIX, CAM_POINT_FIX, CAM_EYEPOS_FIX_THERE,
       CAM_CHARMED_FIX, CAM_TOWER_POS, CAM_FRONT_AND_BACK, CAM_TRUNDLE, CAM_CUBE_PLANET, CAM_OBJ_PARALLEL };  /* tools/camera.py TYPES */

/* What the camera follows (Mario): position, facing, up (against gravity), airborne. */
typedef struct { V3 pos, front, up; int jumping; int flying; V3 fly_dir; } CamTarget; /* flying: a launch star's flight,
                                                                                   * fly_dir its path's direction */

/* Index of the last area (in the blob's priority order) that contains p, or -1 (AreaObjMgr::find_in). */
int camera_area_at(const CamArea *a, int n, V3 p);

/* Areas are in world space (tools/slice.py to_world); camera parameters are zone-local, so each camera computes in
 * its zone's frame (Petari Camera::setZoneMtx): zones = world matrix per zone (column-major), NULL = all identity. */
void camera_init(const CamParam *cams, int ncams, const CamArea *areas, int nareas, const float (*zones)[16],
                 const CamTarget *t);
void camera_update(const CamTarget *t);
void camera_pose(V3 *eye, V3 *watch, V3 *up, float *fovy);
/* SpinDriverCamera / MultiEventCamera: the event cameras "<class>:<set>:NN" one after another (evfrm), their
 * target a matrix (up, front, pos) whose up turns to the flight's direction (updateTargetMatrix); 0 if the class
 * has no such cameras. end: endForceAtLanding (back to the area camera once the target lands). */
unsigned camera_class_hash(const char *name);
int camera_event_start(unsigned cls, int set, V3 up, V3 front, V3 pos);
void camera_event_target(V3 dir, V3 pos);
void camera_event_end(void);
/* a demo's animated camera (CameraAnim): this pose until off */
void camera_set_override(int on, V3 eye, V3 watch, V3 up, float fovy);
/* Active camera (CAMS index, -1 = default) and its type, for the HUD. */
int camera_current(void);
const char *camera_type_name(void);

#endif
