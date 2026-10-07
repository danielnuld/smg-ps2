#include "render.h"
#include "mem.h"

#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <gsInline.h>
#include <gsTexManager.h>
#include <math.h>
#include <string.h>

/* First-light renderer: EE transform, flat shading per triangle, gsKit triangle lists.
 * ponytail: no textures, no strips, no VU1; the frame-time row decides which of these comes first. */

static GSGLOBAL *gs;
static float view[16], proj_x, proj_y;
static float *xf;            /* transformed vertices: x, y, z(view), w per vertex */
static u32 xf_cap;
static GSPRIMPOINT *batch;   /* 3 points per triangle */
static u32 batch_n, tris_drawn;
#define BATCH_TRIS 512
#define NEAR 50.0f
#define SCREEN_W 640.0f
#define SCREEN_H 448.0f

void mat_identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] +
                            a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
    memcpy(out, r, 64);
}

/* SMG placement: translation, then rotation Z * Y * X (degrees), then scale. */
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot[3], const float scale[3])
{
    const float d = 3.14159265f / 180.0f;
    float cx = cosf(rot[0] * d), sx = sinf(rot[0] * d), cy = cosf(rot[1] * d), sy = sinf(rot[1] * d);
    float cz = cosf(rot[2] * d), sz = sinf(rot[2] * d);
    /* R = Rz * Ry * Rx, columns */
    float r00 = cz * cy, r10 = sz * cy, r20 = -sy;
    float r01 = cz * sy * sx - sz * cx, r11 = sz * sy * sx + cz * cx, r21 = cy * sx;
    float r02 = cz * sy * cx + sz * sx, r12 = sz * sy * cx - cz * sx, r22 = cy * cx;
    mat_identity(m);
    m[0] = r00 * scale[0]; m[1] = r10 * scale[0]; m[2] = r20 * scale[0];
    m[4] = r01 * scale[1]; m[5] = r11 * scale[1]; m[6] = r21 * scale[1];
    m[8] = r02 * scale[2]; m[9] = r12 * scale[2]; m[10] = r22 * scale[2];
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2];
}

void render_init(void)
{
    gs = gsKit_init_global_custom(2 * 1024 * 1024, GS_RENDER_QUEUE_PER_POOLSIZE);
    /* 16-bit colour and depth: 2 x 560 KB framebuffers + 560 KB Z leave VRAM for textures and the font
     * (24-bit colour with Z24 takes ~3.4 of the GS's 4 MB). */
    gs->PSM = GS_PSM_CT16S;
    gs->PSMZ = GS_PSMZ_16S;
    gs->ZBuffering = GS_SETTING_ON;
    gs->DoubleBuffering = GS_SETTING_ON;
    gs->PrimAAEnable = GS_SETTING_OFF;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_TexManager_init(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gs->Test->ZTST = 2; /* GEQUAL: larger z is nearer (we write near/w) */
    gsKit_set_test(gs, GS_ZTEST_ON);
    batch = mem_alloc(MEM_RENDER, BATCH_TRIS * 3 * sizeof(GSPRIMPOINT));
    float fovy = 50.0f * 3.14159265f / 180.0f;
    proj_y = (SCREEN_H / 2) / tanf(fovy / 2);
    proj_x = proj_y; /* square pixels at 640x448 */
}

void render_begin(const float eye[3], const float target[3], const float up[3])
{
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    f[0] /= fl; f[1] /= fl; f[2] /= fl;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    /* view: x right, y up, z forward (depth) */
    mat_identity(view);
    view[0] = s[0]; view[4] = s[1]; view[8] = s[2];
    view[1] = u[0]; view[5] = u[1]; view[9] = u[2];
    view[2] = f[0]; view[6] = f[1]; view[10] = f[2];
    view[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    view[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    view[14] = -(f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    gsKit_clear(gs, GS_SETREG_RGBAQ(0x08, 0x0C, 0x28, 0x80, 0x00));
    tris_drawn = 0;
}

static void flush(void)
{
    if (batch_n) {
        gsKit_prim_list_triangle_gouraud_3d(gs, batch_n * 3, batch);
        batch_n = 0;
    }
}

void render_model(const Model *md, const float world[16], u8 cr, u8 cg, u8 cb)
{
    float mv[16];
    mat_mul(mv, view, world);
    /* sphere cull against the near plane and the side planes (rough) */
    float cz = mv[14], rad = md->radius * sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
    if (cz + rad < NEAR)
        return;
    const float L[3] = {0.35f, 0.75f, -0.55f};
    for (u32 g = 0; g < md->ngroups; g++) {
        const Group *gr = &md->groups[g];
        if (gr->nverts > xf_cap) {
            xf_cap = gr->nverts > 4096 ? gr->nverts : 4096;
            xf = mem_alloc(MEM_RENDER, xf_cap * 16);
        }
        for (u32 v = 0; v < gr->nverts; v++) {
            const float *p = gr->verts + 3 * v;
            float x = mv[0] * p[0] + mv[4] * p[1] + mv[8] * p[2] + mv[12];
            float y = mv[1] * p[0] + mv[5] * p[1] + mv[9] * p[2] + mv[13];
            float z = mv[2] * p[0] + mv[6] * p[1] + mv[10] * p[2] + mv[14];
            float *o = xf + 4 * v;
            o[2] = z;
            o[0] = x; o[1] = y;
        }
        for (u32 t = 0; t < gr->ntris; t++) {
            const u16 *ix = gr->idx + 3 * t;
            const float *a = xf + 4 * ix[0], *b = xf + 4 * ix[1], *c = xf + 4 * ix[2];
            if (a[2] < NEAR || b[2] < NEAR || c[2] < NEAR)
                continue;
            float sx[3], sy[3];
            const float *vv[3] = {a, b, c};
            int out = 0;
            for (int k = 0; k < 3; k++) {
                float iw = 1.0f / vv[k][2];
                sx[k] = SCREEN_W / 2 + vv[k][0] * proj_x * iw;
                sy[k] = SCREEN_H / 2 - vv[k][1] * proj_y * iw;
                if (sx[k] < -1500 || sx[k] > 2100 || sy[k] < -1500 || sy[k] > 1900)
                    out = 1;
            }
            if (out || (sx[0] < 0 && sx[1] < 0 && sx[2] < 0) || (sx[0] > SCREEN_W && sx[1] > SCREEN_W && sx[2] > SCREEN_W) ||
                (sy[0] < 0 && sy[1] < 0 && sy[2] < 0) || (sy[0] > SCREEN_H && sy[1] > SCREEN_H && sy[2] > SCREEN_H))
                continue;
            float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
            float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) + 1e-6f;
            float lit = fabsf(n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / nl; /* two-sided */
            float k = 0.25f + 0.75f * lit;
            gs_rgbaq col = color_to_RGBAQ(cr * k, cg * k, cb * k, 0x80, 0.0f);
            GSPRIMPOINT *pt = batch + 3 * batch_n;
            for (int q = 0; q < 3; q++) {
                int iz = (int)(65535.0f * NEAR / vv[q][2]);
                pt[q].rgbaq = col;
                pt[q].xyz2 = vertex_to_XYZ2(gs, sx[q], sy[q], iz);
            }
            tris_drawn++;
            if (++batch_n == BATCH_TRIS)
                flush();
        }
    }
    flush();
}

/* 3x5 bitmap font drawn with sprites: no texture, no VRAM, no BIOS font (gsKit_init_fontm fails here).
 * Each glyph is 5 rows of 3 bits, top row first. Digits, upper-case letters and a few signs. */
static const char *glyph_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:%-/ ";
static const u16 glyphs[] = {
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075711,   /* 0-9 */
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011157,   /* A-J */
    055655, 044447, 057555, 065555, 025552, 065644, 025573, 065655, 034716, 072222,   /* K-T */
    055557, 055552, 055575, 055255, 055222, 071247, 000020, 002020, 041241, 000700,   /* U-Z . : % - */
    001240, 000000};                                                                     /* / space */

void render_text(float x, float y, const char *s)
{
    const float px = 2.0f;
    u64 col = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
    for (; *s; s++, x += 4 * px) {
        char ch = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        const char *at = strchr(glyph_chars, ch);
        if (!at)
            continue;
        u16 g = glyphs[at - glyph_chars];
        for (int row = 0; row < 5; row++)
            for (int c = 0; c < 3; c++)
                if (g >> (3 * (4 - row) + (2 - c)) & 1)
                    gsKit_prim_sprite(gs, x + c * px, y + row * px, x + (c + 1) * px, y + (row + 1) * px, 0xFFFF, col);
    }
}

void render_end(void)
{
    flush();
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
    gsKit_TexManager_nextFrame(gs);
}

u32 render_tris_drawn(void) { return tris_drawn; }
