#ifndef RENDER_H
#define RENDER_H

#include "blob.h"

typedef struct { float m[16]; } Mat4; /* column-major, v' = M v */

void render_init(void);
/* Vertical field of view in degrees (SMG's camera default is 45). */
void render_set_fovy(float deg);
/* the projection translated by (x, y) in normalized device units, y up (camera shakes) */
void render_set_shake(float x, float y);
/* the environment-mapped groups' VU1Env matrices for this frame's projection (after render_set_fovy / shake) */
void render_env(const Scene *s);
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
/* the light lit materials are drawn with from here on (LightFunction::loadActorLightInfo): two lights at world
 * positions (each draw takes its direction from the instance), their colours and the ambient, 0..1 */
typedef struct { float pos[2][3], col[2][3], amb[3]; } RenderLight;
void render_light(const RenderLight *l);
/* BloomEffect (Petari): the frame's bloom models (DrawBufferType_BloomModel) and its strength this frame (BloomArea:
 * intensity 0 off, blur intensities 1 / 2), drawn at render_end */
void render_bloom_model(const Model *md, const float world[16]);
void render_bloom(u8 intensity, u8 intensity1, u8 intensity2);
/* a textured triangle list projected on the EE (OceanSphere): world positions, texture coordinates (0..1 over the
 * texture, repeating if it does), ntri x 3 indices; blend: 0 opaque writing depth, 1 alpha blended without depth
 * writes; triangles with a corner behind the camera are left out */
void render_mesh(const float (*pos)[3], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, int blend);
/* the same in a colour 0xAABBGGRR (0x80 = 1.0) modulating the texture; blend 2 additive (Cs x As + Cd) */
/* the next render_mesh / render_mesh_c draws the same vertices as the last one: their projection is kept */
void render_mesh_same(void);
void render_mesh_c(const float (*pos)[3], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, int blend, u32 rgba);
/* render_mesh on the screen: x 0..640, y 0..448 (HUD units), over everything; colour 0xAABBGGRR (0x80 = 1.0)
 * modulating the texture, additive (Cs x As + Cd) or blended */
void render_mesh2d(const float (*xy)[2], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, u32 rgba, int add);
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
/* 2D quads over the frame, drawn after everything else in the chain (before the HUD text): x 0..640, y 0..448,
 * u / v / uw / vh in texels, colour 0xAABBGGRR (0x80 = 1.0) modulating the texture, tex NO_TEX for flat; vw / uh
 * (v along the width, u down the height) skew the texture (a rotated texture SRT): drawn as two triangles with
 * float coordinates (clamped off the texture), as is a quad with pad 1 */
typedef struct { float x, y, w, h, u, v, uw, vh; u32 rgba; u16 tex, pad; float vw, uh; } Quad;
void render_quads(const Quad *q, int n);
/* the quads queued so far drawn now (in the chain's order: what comes after covers them) */
void render_quads_flush(void);
/* the world finished now (its blended groups, the bloom, the queued 2D): the menus are drawn over it */
void render_world_done(void);
/* a drop shadow: a half-dark disc of radius 1 in the matrix's XZ plane (drawn with the blended instances) */
void render_shadow(const float world[16]);
/* Finish the frame (VIF chain, HUD, wait for the GS) and flip; returns EE cycles spent before the vsync wait. */
u32 render_end(void);
/* Depth reset to far over the screen (after the sky): later draws always cover what is there */
void render_clear_depth(void);
u32 render_tris_drawn(void);
u32 render_free_block(void); /* movie.c: the texture cache's first VRAM block (64 words), free with no scene loaded */
void render_picture(const void *upload, int w, int h, u32 fade); /* a movie picture (upload: its GIF chain), letterboxed */
void render_show(void);      /* the frame drawn shown (movie.c's pacing) */
/* a world-space sphere within the camera's view (the test draw_model culls by) */
int render_clip_judge(const float p[3], float rad); /* off ClippingJudge's volume (near 500, far 10000) */
int render_visible(const float p[3], float rad);
u32 render_wait_cycles(void); /* EE cycles blocked on the VIF chain since the last call (profiling) */
/* Texture cache counters since boot: uploads (misses), textures that found no GS memory, chains waited on for room. */
void render_tex_stats(u32 *misses, u32 *overflow, u32 *syncs);

void mat_identity(float m[16]);
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot_deg[3], const float scale[3]);
void mat_mul(float out[16], const float a[16], const float b[16]);   /* on VU0 */
void mat_mul_c(float out[16], const float a[16], const float b[16]); /* in C (its check) */

#endif
