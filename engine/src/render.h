#ifndef RENDER_H
#define RENDER_H

#include "blob.h"

typedef struct { float m[16]; } Mat4; /* column-major, v' = M v */

void render_init(void);
void render_begin(const float eye[3], const float target[3], const float up[3]);
/* Draw a model with world matrix `world` (column-major 4x4), flat-shaded with a base colour. */
void render_model(const Model *md, const float world[16], u8 r, u8 g, u8 b);
void render_text(float x, float y, const char *s);
/* Finish the frame (VIF chain, HUD, wait for the GS) and flip; returns EE cycles spent before the vsync wait. */
u32 render_end(void);
u32 render_tris_drawn(void);

void mat_identity(float m[16]);
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot_deg[3], const float scale[3]);
void mat_mul(float out[16], const float a[16], const float b[16]);

#endif
