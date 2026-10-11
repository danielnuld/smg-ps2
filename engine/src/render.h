#ifndef RENDER_H
#define RENDER_H

#include "blob.h"

typedef struct { float m[16]; } Mat4; /* column-major, v' = M v */

void render_init(void);
/* Vertical field of view in degrees (SMG's camera default is 45). */
void render_set_fovy(float deg);
/* Upload the scene's textures from index first_tex on to VRAM (their GS state packets too) and build the prebuilt
 * chains of the models from first_model on. Call after blob_load / blob_add (0, 0 the first time). */
void render_load_textures(const Scene *s, u32 first_tex, u32 first_model);
/* Undo render_load_textures from these indices: frees those models' chains and rewinds the texture VRAM. Waits for
 * the GS and the VIF first, so nothing in flight still reads them. */
void render_release(Scene *s, u32 first_tex, u32 first_model);
void render_begin(const float eye[3], const float target[3], const float up[3]);
/* Draw a model with world matrix `world` (column-major 4x4), flat-shaded with a base colour. */
void render_model(const Model *md, const float world[16], u8 r, u8 g, u8 b);
/* Draw a skinned model with its draw matrices (anim.c Anim.drw). */
void render_skinned(const Model *md, const float world[16], const float (*drw)[16]);
void render_text(float x, float y, const char *s);
/* A filled rectangle over everything, in the HUD's units (x 0..640, y 0..448) */
void render_rect(float x, float y, float w, float h, u8 r, u8 g, u8 b);
/* Camera-facing textured squares (particles): centre, half size in world units, colour 0xAABBGGRR (0x80 = 1.0),
 * scene texture, additive (else alpha) blending. Depth tested, not written; draw them after the models. */
typedef struct { float pos[3], half; u32 rgba; u16 tex; u8 add, pad; } Sprite;
void render_sprites(const Sprite *sp, int n);
/* a vertical textured strip: 4 levels of 2 points (left, right; bottom first), v 1 at the bottom, 0 at the top */
typedef struct { float p[4][2][3]; u32 rgba; u16 tex; u16 pad; } Strip;
void render_strips(const Strip *st, int n);
/* Finish the frame (VIF chain, HUD, wait for the GS) and flip; returns EE cycles spent before the vsync wait. */
u32 render_end(void);
/* Depth reset to far over the screen (after the sky): later draws always cover what is there */
void render_clear_depth(void);
u32 render_tris_drawn(void);
/* Texture cache counters since boot: uploads (misses), textures that found no GS memory, chains waited on for room. */
void render_tex_stats(u32 *misses, u32 *overflow, u32 *syncs);

void mat_identity(float m[16]);
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot_deg[3], const float scale[3]);
void mat_mul(float out[16], const float a[16], const float b[16]);

#endif
