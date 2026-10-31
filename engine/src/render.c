#include "render.h"
#include "mem.h"

#include <dma.h>
#include <draw.h>
#include <draw2d.h>
#include <draw3d.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <malloc.h>
#include <math.h>
#include <packet2.h>
#include <packet2_utils.h>
#include <stdio.h>
#include <string.h>

/* console diagnosis (ORBIT, 2026-10-23: the console froze after the first 3D frame, PCSX2 did not): every DMA / GS
 * FINISH wait in this file prints the DMAC, VIF1, GIF and GS state once a second while it is stuck */
static u32 dbg_count(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
#define DBG_R(a) (*(volatile u32 *)(a))
static void dbg_dump(const char *what, int line)
{
    printf("stuck %s line %d: D1 chcr %08x madr %08x qwc %08x tadr %08x | D2 chcr %08x madr %08x qwc %08x | "
           "VIF1 stat %08x err %08x code %08x | GIF stat %08x | D_STAT %08x | GS CSR %08x\n", what, line,
           DBG_R(0x10009000), DBG_R(0x10009010), DBG_R(0x10009020), DBG_R(0x10009030), DBG_R(0x1000A000),
           DBG_R(0x1000A010), DBG_R(0x1000A020), DBG_R(0x10003C00), DBG_R(0x10003C20), DBG_R(0x10003C80),
           DBG_R(0x10003020), DBG_R(0x1000E010), DBG_R(0x12001000));
}
static void dbg_dma_wait(int ch, int line)
{
    volatile u32 *chcr = (volatile u32 *)(ch == DMA_CHANNEL_GIF ? 0x1000A000 : 0x10009000);
    u32 c0 = dbg_count();
    int n = 0;
    while (*chcr & 0x100)
        if (dbg_count() - c0 > 294912000u && n < 5) { dbg_dump(ch == DMA_CHANNEL_GIF ? "GIF dma" : "VIF1 dma", line); c0 = dbg_count(); n++; }
}
static void dbg_finish_wait(int line)
{
    u32 c0 = dbg_count();
    int n = 0;
    while (!(DBG_R(0x12001000) & 2))
        if (dbg_count() - c0 > 294912000u && n < 5) { dbg_dump("GS finish", line); c0 = dbg_count(); n++; }
    DBG_R(0x12001000) = 2; /* as draw_wait_finish: the FINISH event cleared */
}
#define dma_channel_wait(ch, t) dbg_dma_wait(ch, __LINE__)
#define draw_wait_finish() dbg_finish_wait(__LINE__)

/* 16-bit frames: dithering on (the usual 4 x 4 matrix), after each draw_setup_environment (which turns it off) */
static qword_t *env_dither(qword_t *q)
{
#ifndef LORES
    static char dm[16] = {-4, 2, -3, 3, 0, -2, 1, -1, -3, 3, -4, 2, 1, -1, 0, -2};
    q = draw_dither_matrix(q, dm);
    q = draw_dithering(q, 1);
#endif
    return q;
}

/* VU1 renderer: the EE only builds DMA chains that point at the blob's VU-ready vertex data (VUMD chunk) and at one
 * matrix per model instance; VU1 (vu1_tex.vsm) transforms, rejects off-band triangles, projects, computes STQ and
 * XGKICKs to the GS. Set-up follows ps2sdk samples/draw/vu1 (libpacket2, libdraw, libgraph).
 * Textures (TEXT chunk, PSMT4 + 16-entry CLUT) are uploaded to VRAM once at load: Gateway's set fits the VRAM left
 * after two frames and Z (ponytail: per-frame upload over PATH3 when a scene's set does not fit, milestone 5).
 * A texture change is a FLUSH + DIRECT A+D packet (TEX0 with CLUT load, TEX1, CLAMP, TEST) in the VIF chain. */

extern u32 VU1Tex_CodeStart __attribute__((section(".vudata")));
extern u32 VU1Tex_CodeEnd __attribute__((section(".vudata")));
extern u32 VU1Lit_Start __attribute__((section(".vudata")));
#define LIT_PROGRAM ((u32)((u8 *)&VU1Lit_Start - (u8 *)&VU1Tex_CodeStart) / 8) /* MSCAL address of the lit batches */
extern u32 VU1Env_Start __attribute__((section(".vudata")));
#define ENV_PROGRAM ((u32)((u8 *)&VU1Env_Start - (u8 *)&VU1Tex_CodeStart) / 8) /* environment-mapped batches */
extern u32 VU1Strip_Start __attribute__((section(".vudata")));
#define STRIP_PROGRAM ((u32)((u8 *)&VU1Strip_Start - (u8 *)&VU1Tex_CodeStart) / 8) /* static strips (blob.h strip) */
extern u32 VU1SkinLit_Start __attribute__((section(".vudata")));
#define SKINLIT_PROGRAM ((u32)((u8 *)&VU1SkinLit_Start - (u8 *)&VU1Tex_CodeStart) / 8) /* skinned lit strips */
extern u32 VU1EnvStrip_Start __attribute__((section(".vudata")));
#define ENVSTRIP_PROGRAM ((u32)((u8 *)&VU1EnvStrip_Start - (u8 *)&VU1Tex_CodeStart) / 8) /* static env strips */
#define PROGRAM(vg) ((vg)->strip == 3 ? ENVSTRIP_PROGRAM : (vg)->strip == 2 ? SKINLIT_PROGRAM : (vg)->strip ? STRIP_PROGRAM :                      (vg)->lit == 2 ? ENV_PROGRAM : (vg)->lit ? LIT_PROGRAM : 0)

#define SCREEN_W 640
/* 640 x 224 buffers shown in FRAME mode (each field reads every line): the picture is the same on both fields, so it
 * does not bob a line per frame under PCSX2's deinterlacing (a 448-line buffer read every other line did: each
 * buffer always lands on the same field). Layout and projection stay in 448-line units (DISP_H), scaled by YS. */
#ifndef LORES /* HIRES (default): 640 x 448, 16-bit colour (dithered) and depth: the same VRAM as 640 x 224 at 32 bits */
#define SCREEN_H 448
#define FB_PSM GS_PSM_16S
#define Z_FMT GS_ZBUF_16S
#define Z_MAX 0xFFFF      /* the nearest depth (ZTEST GEQUAL: near is large) */
#define Z_SCALE ((float)Z_MAX / 32.0f)
#define FB_TH 9           /* log2 of the texture height that covers the frame (bloom's source) */
#else
#define SCREEN_H 224
#define FB_PSM GS_PSM_32
#define Z_FMT GS_ZBUF_24
#define Z_MAX 0xFFFFFF
#define Z_SCALE ((float)Z_MAX / 32.0f)
#define FB_TH 8
#endif
#define DISP_H 448
#define YS ((float)SCREEN_H / DISP_H)

/* Asynchronous flip: render_end puts the HUD and a FINISH at the end of the frame's VIF1 chain and returns; the
 * VBLANK handler shows the frame once the GS has signalled FINISH, so the next frame's logic overlaps the VU1 / GS
 * work and no frame waits for a vblank of its own (a 35 ms frame no longer costs three). Whatever draws into a
 * frame buffer, or changes VRAM, first waits for the flip (render_flip_wait). */
static volatile int flip_pending;
static volatile u32 flip_addr;
static int fb_psm = FB_PSM;  /* the frames' format: GS_PSM_32 while a movie plays (render_movie_mode) */
volatile u32 render_vblanks; /* every vblank (bench: real frame rate) */
static int flip_tick(int cause)
{
    (void)cause;
    render_vblanks++;
    if (flip_pending && (*(volatile u64 *)0x12001000 & 2)) { /* GS CSR: FINISH */
        *(volatile u64 *)0x12001000 = 2;
        graph_set_framebuffer(0, flip_addr, SCREEN_W, fb_psm, 0, 0);
        flip_pending = 0;
    }
    ExitHandler();
    return 0;
}
void render_flip_wait(void)
{
    while (flip_pending) {}
}

#define BATCH 60           /* vertices per VU1 batch: 7n + 7m + 2 qwords (lit, m <= 10 matrices) must fit a 496-qword
                            * buffer, and n a multiple of 12 so the 4-byte colour, UV and normal arrays of the next
                            * batch start on a qword */
#define NEAR 50.0f
#define FAR 120000.0f
#define MAX_INST 1024
#define PACKET_QW 16384

static framebuffer_t frame[2];   /* double buffered: draw into frame[back], show the other one */
static int back;
static zbuffer_t zbuf;
static packet2_t *vif[2], *gif;
#define GIF_QW 8192             /* HUD text: one sprite per lit font pixel (the boot menu fills it) */
static int cur;
static float view[16], proj[16];
static float pv[16] __attribute__((aligned(16))); /* proj x view (draw_model), pv_ok 0 after either changes */
static int pv_ok;
static float shake_x, shake_y; /* render_set_shake, in pixels */
static u32 bloom_q, bloom_q2, bloom_e; /* VRAM (words) of the bloom buffers: 160 x 56 (FBW 3), again, 80 x 28 (FBW 2) */
static float frustum[4];        /* side planes in view space: x n, z n for |x| and |y| (render_set_fovy) */
#define MAT_FL 28                /* a matrix as VU1 reads it: 4 columns local -> clip, then the light matrix's 3 */
static float (*mats)[MAT_FL];    /* one matrix per instance this frame, read by DMA */
static int nmats;
#define MAX_MAT 10               /* matrices per batch (a J3D matrix group holds up to 10) */
#define HDR(t, m, n) (headers + 2 * (((t) * MAX_MAT + (m) - 1) * (BATCH / 3 + 1) + (n) / 3))
static qword_t *headers;         /* [mode][matrices 1..MAX_MAT][count 3..BATCH]: {n, 4m} ints + GIF tag; mode
                                  * bit 0 textured, bit 1 alpha blended (group_mode) */
#define MODES 4
static qword_t scale_qw __attribute__((aligned(16)));
static float uv_ident[12] __attribute__((aligned(16))) = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}; /* VU mem 8..10 */
static float light_qw[16] __attribute__((aligned(16)));          /* VU mem 11..14: render_light */
static const RenderLight *light, *light_sent;                     /* the current light, the one VU mem holds */
#define HDRS(t, n) (headers_static + 2 * ((t) * (BATCH / 3 + 1) + (n) / 3))
static qword_t *headers_static;  /* [mode][count]: {n, 0, matrix base 1} + GIF tag, for prebuilt chains */
#define STRIP_MAX 80             /* VU1Strip: vertices per batch (vu1_tex.vsm's fixed places; tools/slice.py STRIP_MAX) */
#define HSTRIP(t, n) (headers_strip + 2 * ((t) * (STRIP_MAX / 4 + 1) + (n) / 4))
static qword_t *headers_strip;   /* [mode][count / 4]: {n} + GIF tag of a TRIANGLE_STRIP */
static u32 tris_drawn;
u32 render_vstat[6]; /* emitted vertices by VU1 path (bench) */
static u32 vram_tex;             /* VRAM words taken by textures and CLUTs */
static qword_t *tex_packets;     /* per texture 8 qwords (GIF tag + TEX0, TEX1, CLAMP, TEST, ALPHA, ZBUF, MIPTBP1), then 3
                                  * for NO_TEX (tag + TEST, ZBUF). Blended textures (alpha kind 2) blend
                                  * (Cs - Cd) As + Cd and leave the depth alone; the rest write it */
#define TEXP 8
#define LOD_BIAS 32 /* 1/16 levels added to every K: surfaces seen edge-on shrink more than the GS depth LOD says */
#define VRAM_WORDS (1 << 20)        /* 4 MB of GS memory in 32-bit words */
#define NO_TEX_PACKET (tex_packets + TEXP * SCENE_MAX_TEX)
static int cur_tex;
static u64 test_z;               /* TEST with the depth test only */

void mat_identity(float m[16]) /* stored one by one: memset's call costs more than the 16 stores */
{
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 0.0f;
    m[4] = 0.0f; m[5] = 1.0f; m[6] = 0.0f; m[7] = 0.0f;
    m[8] = 0.0f; m[9] = 0.0f; m[10] = 1.0f; m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

void mat_mul_c(float out[16], const float a[16], const float b[16]) /* the plain C one (mat_mul's check) */
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++)
            r[c * 4 + rr] = a[0 * 4 + rr] * b[c * 4 + 0] + a[1 * 4 + rr] * b[c * 4 + 1] +
                            a[2 * 4 + rr] * b[c * 4 + 2] + a[3 * 4 + rr] * b[c * 4 + 3];
    memcpy(out, r, 64);
}

/* out = a b (column-major) on VU0 in macro mode: each of b's columns as a's columns weighted by its x y z w (ACC);
 * lqc2 / sqc2 need 16-byte aligned matrices: unaligned ones go through aligned copies. All loads come before the
 * stores, so out may be a or b */
static inline void mat_mul_vu0(float *R, const float *A, const float *B)
{
    __asm__ volatile(
        "lqc2 $vf1, 0(%1)\n"
        "lqc2 $vf2, 16(%1)\n"
        "lqc2 $vf3, 32(%1)\n"
        "lqc2 $vf4, 48(%1)\n"
        "lqc2 $vf5, 0(%2)\n"
        "lqc2 $vf6, 16(%2)\n"
        "lqc2 $vf7, 32(%2)\n"
        "lqc2 $vf8, 48(%2)\n"
        "vmulax.xyzw $ACC, $vf1, $vf5x\n"
        "vmadday.xyzw $ACC, $vf2, $vf5y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf5z\n"
        "vmaddw.xyzw $vf5, $vf4, $vf5w\n"
        "vmulax.xyzw $ACC, $vf1, $vf6x\n"
        "vmadday.xyzw $ACC, $vf2, $vf6y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf6z\n"
        "vmaddw.xyzw $vf6, $vf4, $vf6w\n"
        "vmulax.xyzw $ACC, $vf1, $vf7x\n"
        "vmadday.xyzw $ACC, $vf2, $vf7y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf7z\n"
        "vmaddw.xyzw $vf7, $vf4, $vf7w\n"
        "vmulax.xyzw $ACC, $vf1, $vf8x\n"
        "vmadday.xyzw $ACC, $vf2, $vf8y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf8z\n"
        "vmaddw.xyzw $vf8, $vf4, $vf8w\n"
        "sqc2 $vf5, 0(%0)\n"
        "sqc2 $vf6, 16(%0)\n"
        "sqc2 $vf7, 32(%0)\n"
        "sqc2 $vf8, 48(%0)\n"
        : : "r"(R), "r"(A), "r"(B) : "memory");
}
void mat_mul(float out[16], const float a[16], const float b[16])
{
    float A[16] __attribute__((aligned(16))), B[16] __attribute__((aligned(16))), R[16] __attribute__((aligned(16)));
    const float *pa = a, *pb = b;
    if ((u32)a & 15) { memcpy(A, a, 64); pa = A; }
    if ((u32)b & 15) { memcpy(B, b, 64); pb = B; }
    if ((u32)out & 15) { mat_mul_vu0(R, pa, pb); memcpy(out, R, 64); }
    else mat_mul_vu0(out, pa, pb);
}

static void scale_cols(float m[16], float s);
/* mat_mul then scale_cols(out, s) (a skinned draw's matrices: columns 0-2 x the model's quantisation), in one go on
 * VU0 when all three are aligned */
static void mat_mul_scale3(float *out, const float *a, const float *b, float s)
{
    if (((u32)out | (u32)a | (u32)b) & 15) { mat_mul(out, a, b); scale_cols(out, s); return; }
    __asm__ volatile(
        "qmtc2 %3, $vf9\n"
        "lqc2 $vf1, 0(%1)\n lqc2 $vf2, 16(%1)\n lqc2 $vf3, 32(%1)\n lqc2 $vf4, 48(%1)\n"
        "lqc2 $vf5, 0(%2)\n lqc2 $vf6, 16(%2)\n lqc2 $vf7, 32(%2)\n lqc2 $vf8, 48(%2)\n"
        "vmulax.xyzw $ACC, $vf1, $vf5x\n vmadday.xyzw $ACC, $vf2, $vf5y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf5z\n vmaddw.xyzw $vf5, $vf4, $vf5w\n"
        "vmulax.xyzw $ACC, $vf1, $vf6x\n vmadday.xyzw $ACC, $vf2, $vf6y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf6z\n vmaddw.xyzw $vf6, $vf4, $vf6w\n"
        "vmulax.xyzw $ACC, $vf1, $vf7x\n vmadday.xyzw $ACC, $vf2, $vf7y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf7z\n vmaddw.xyzw $vf7, $vf4, $vf7w\n"
        "vmulax.xyzw $ACC, $vf1, $vf8x\n vmadday.xyzw $ACC, $vf2, $vf8y\n"
        "vmaddaz.xyzw $ACC, $vf3, $vf8z\n vmaddw.xyzw $vf8, $vf4, $vf8w\n"
        "vmulx.xyzw $vf5, $vf5, $vf9x\n vmulx.xyzw $vf6, $vf6, $vf9x\n vmulx.xyzw $vf7, $vf7, $vf9x\n"
        "sqc2 $vf5, 0(%0)\n sqc2 $vf6, 16(%0)\n sqc2 $vf7, 32(%0)\n sqc2 $vf8, 48(%0)\n"
        : : "r"(out), "r"(a), "r"(b), "r"(*(u32 *)&s) : "memory");
}

/* SMG placement: translation, then rotation Z * Y * X (degrees), then scale. */
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot[3], const float scale[3])
{
    const float d = 3.14159265f / 180.0f;
    float cx = cosf(rot[0] * d), sx = sinf(rot[0] * d), cy = cosf(rot[1] * d), sy = sinf(rot[1] * d);
    float cz = cosf(rot[2] * d), sz = sinf(rot[2] * d);
    float r00 = cz * cy, r10 = sz * cy, r20 = -sy;
    float r01 = cz * sy * sx - sz * cx, r11 = sz * sy * sx + cz * cx, r21 = cy * sx;
    float r02 = cz * sy * cx + sz * sx, r12 = sz * sy * cx - cz * sx, r22 = cy * cx;
    mat_identity(m);
    m[0] = r00 * scale[0]; m[1] = r10 * scale[0]; m[2] = r20 * scale[0];
    m[4] = r01 * scale[1]; m[5] = r11 * scale[1]; m[6] = r21 * scale[1];
    m[8] = r02 * scale[2]; m[9] = r12 * scale[2]; m[10] = r22 * scale[2];
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2];
}

static void send_vif_wait(packet2_t *p)
{
    dma_channel_send_packet2(p, DMA_CHANNEL_VIF1, 1);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
}

static void init_blend_states(void);

static void shadow_build(void);

void render_init(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0);
    /* VIF1_ERR.ME0: our chains send an UNPACK's data through REF tags, and with TTE on the real VIF1 takes such a tag
     * arriving mid-UNPACK as a DMAtag mismatch (VIF1_STAT ER0) and stalls; masked, it ignores the tag. PCSX2 does not
     * emulate ER0: the console froze on the first 3D frame (VIF1 stat 0e0010c3, 2026-10-23) */
    *(volatile u32 *)0x10003C20 = 2;
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    dma_channel_fast_waits(DMA_CHANNEL_VIF1);

    /* microprogram + double buffer (base 16, 496 qwords each; ps2sdk's sample has 8: VU mem 1..14 hold constants) */
    u32 sz = packet2_utils_get_packet_size_for_program(&VU1Tex_CodeStart, &VU1Tex_CodeEnd) + 1;
    packet2_t *p = packet2_create(sz, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_vif_add_micro_program(p, 0, &VU1Tex_CodeStart, &VU1Tex_CodeEnd);
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);
    p = packet2_create(2, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_utils_vu_add_double_buffer(p, 16, 496);
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);

    for (int i = 0; i < 2; i++) {
        frame[i].width = SCREEN_W; frame[i].height = SCREEN_H; frame[i].mask = 0; frame[i].psm = FB_PSM;
        frame[i].address = graph_vram_allocate(SCREEN_W, SCREEN_H, FB_PSM, GRAPH_ALIGN_PAGE);
    }
    zbuf.enable = DRAW_ENABLE; zbuf.mask = 0; zbuf.method = ZTEST_METHOD_GREATER_EQUAL; zbuf.zsm = Z_FMT;
    zbuf.address = graph_vram_allocate(SCREEN_W, SCREEN_H, Z_FMT, GRAPH_ALIGN_PAGE);
    bloom_q = graph_vram_allocate(192, 64, GS_PSM_32, GRAPH_ALIGN_PAGE);  /* bloom: a quarter, its blur, an eighth */
    bloom_q2 = graph_vram_allocate(192, 64, GS_PSM_32, GRAPH_ALIGN_PAGE);
    bloom_e = graph_vram_allocate(128, 32, GS_PSM_32, GRAPH_ALIGN_PAGE);
    graph_initialize(frame[0].address, SCREEN_W, SCREEN_H, FB_PSM, 0, 0);
    /* 224 lines: FRAME (each field reads every line); 448: FIELD (each field its alternate lines: true 480i) */
    graph_set_mode(GRAPH_MODE_INTERLACED, graph_get_region(), SCREEN_H == DISP_H ? GRAPH_MODE_FIELD : GRAPH_MODE_FRAME,
                   GRAPH_DISABLE);
    graph_set_screen(0, 16, SCREEN_W, DISP_H);
    graph_set_framebuffer(0, frame[0].address, SCREEN_W, FB_PSM, 0, 0);
    graph_set_output(GRAPH_ENABLE, GRAPH_DISABLE, 1, 0, 0, 0xFF); /* circuit 1, alpha from ALP (frame alpha is 0x80) */
    back = 1;
    AddIntcHandler(INTC_VBLANK_S, flip_tick, 0);
    EnableIntc(INTC_VBLANK_S);
    p = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_update(p, draw_setup_environment(p->next, 0, &frame[back], &zbuf));
    packet2_update(p, env_dither(p->next));
    packet2_update(p, draw_primitive_xyoffset(p->next, 0, 2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2));
    packet2_update(p, draw_finish(p->next));
    dma_channel_send_packet2(p, DMA_CHANNEL_GIF, 1);
    dma_wait_fast();
    packet2_free(p);

    /* packets, matrices and batch headers: charged to the render runtime */
    vif[0] = packet2_create(PACKET_QW, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    vif[1] = packet2_create(PACKET_QW, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    gif = packet2_create(GIF_QW, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    mem_charge(MEM_RENDER, 2 * PACKET_QW * 16 + GIF_QW * 16);
    mats = mem_alloc(MEM_RENDER, MAX_INST * MAT_FL * 4);
    headers = mem_alloc(MEM_RENDER, MODES * MAX_MAT * (BATCH / 3 + 1) * 2 * 16);
    for (int t = 0; t < MODES; t++)
        for (int m = 1; m <= MAX_MAT; m++)
            for (int n = 3; n <= BATCH; n += 3) {
                qword_t *h = HDR(t, m, n);
                h->sw[0] = n;
                h->sw[1] = 7 * m;
                h[1].dw[0] = GIF_SET_TAG(n, 1, 1, GS_SET_PRIM(PRIM_TRIANGLE, 1, t & 1, 0, t >> 1, 0, 0, 0, 0), GIF_FLG_PACKED, 3);
                h[1].dw[1] = (u64)GIF_REG_ST | (u64)GIF_REG_RGBAQ << 4 | (u64)GIF_REG_XYZ2 << 8; /* ST first: Q */
            }
    headers_static = mem_alloc(MEM_RENDER, MODES * (BATCH / 3 + 1) * 2 * 16);
    for (int t = 0; t < MODES; t++)
        for (int n = 3; n <= BATCH; n += 3) {
            qword_t *h = HDRS(t, n);
            *h = *HDR(t, 1, n);
            h->sw[1] = 0;
            h->sw[2] = 1;
            h[1] = HDR(t, 1, n)[1];
        }
    headers_strip = mem_alloc(MEM_RENDER, MODES * (STRIP_MAX / 4 + 1) * 2 * 16);
    for (int t = 0; t < MODES; t++)
        for (int n = 4; n <= STRIP_MAX; n += 4) {
            qword_t *h = HSTRIP(t, n);
            h->sw[0] = n; h->sw[1] = 0; h->sw[2] = 1; h->sw[3] = 0;
            h[1].dw[0] = GIF_SET_TAG(n, 1, 1, GS_SET_PRIM(PRIM_TRIANGLE_STRIP, 1, t & 1, 0, t >> 1, 0, 0, 0, 0), GIF_FLG_PACKED, 3);
            h[1].dw[1] = (u64)GIF_REG_ST | (u64)GIF_REG_RGBAQ << 4 | (u64)GIF_REG_XYZ2 << 8;
        }
    /* scale constant at VU mem 0, below the double buffers: written once */
    ((float *)&scale_qw)[0] = 2048.0f;
    ((float *)&scale_qw)[1] = 2048.0f;
    ((float *)&scale_qw)[2] = Z_SCALE;
    p = packet2_create(6, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_utils_vu_add_unpack_data(p, 0, &scale_qw, 1, 0);
    packet2_utils_vu_add_unpack_data(p, 8, uv_ident, 3, 0); /* the UV matrix, identity */
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);
    test_z = GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL);
    init_blend_states();
    { static Scene none; render_load_textures(&none, 0, 0); } /* the texture slots, once */
    shadow_build();

    render_set_fovy(45.0f);
}

/* projection (column-major, v' = P v): x, y to the GS's +-2048 guard band, depth near -> 1, far -> -1 */
void render_set_fovy(float deg)
{
    float f = (DISP_H / 2) / tanf(deg * 3.14159265f / 360.0f);
    float B = 2.0f / (1.0f / NEAR - 1.0f / FAR), A = 1.0f - B / NEAR;
    pv_ok = 0;
    memset(proj, 0, sizeof proj);
    proj[0] = f / 2048.0f;
    proj[5] = -f * YS / 2048.0f;
    proj[10] = A; proj[11] = 1.0f;
    proj[14] = B;
    proj[8] = shake_x / 2048.0f; proj[9] = shake_y / 2048.0f; /* x / w gains the shift (w = view z) */
    float lx = sqrtf(f * f + (SCREEN_W / 2) * (SCREEN_W / 2)), ly = sqrtf(f * f + (DISP_H / 2) * (DISP_H / 2));
    frustum[0] = f / lx; frustum[1] = (SCREEN_W / 2) / lx;
    frustum[2] = f / ly; frustum[3] = (DISP_H / 2) / ly;
}

void render_set_shake(float x, float y)
{
    shake_x = x * (SCREEN_W / 2);
    shake_y = -y * (SCREEN_H / 2);
    proj[8] = shake_x / 2048.0f; proj[9] = shake_y / 2048.0f;
    pv_ok = 0;
}

u32 render_begin_wait; /* EE cycles render_begin waited for the flip, the VIF and the clear (bench) */
void render_begin(const float eye[3], const float target[3], const float up[3])
{
    float fw[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float fl = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
    fw[0] /= fl; fw[1] /= fl; fw[2] /= fl;
    float s[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = {s[1] * fw[2] - s[2] * fw[1], s[2] * fw[0] - s[0] * fw[2], s[0] * fw[1] - s[1] * fw[0]};
    pv_ok = 0;
    mat_identity(view);
    view[0] = s[0]; view[4] = s[1]; view[8] = s[2];
    view[1] = u[0]; view[5] = u[1]; view[9] = u[2];
    view[2] = fw[0]; view[6] = fw[1]; view[10] = fw[2];
    view[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    view[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    view[14] = -(fw[0] * eye[0] + fw[1] * eye[1] + fw[2] * eye[2]);

    u32 bw0;
    __asm__ volatile("mfc0 %0, $9" : "=r"(bw0));
    render_flip_wait(); /* frame[back] is still on screen until the last frame's flip */
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_reset(gif, 0);
    packet2_update(gif, draw_setup_environment(gif->next, 0, &frame[back], &zbuf));
    packet2_update(gif, env_dither(gif->next));
    packet2_update(gif, draw_primitive_xyoffset(gif->next, 0, 2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2));
    packet2_update(gif, draw_disable_tests(gif->next, 0, &zbuf));
    packet2_update(gif, draw_clear(gif->next, 0, 2048.0f - SCREEN_W / 2, 2048.0f - SCREEN_H / 2, SCREEN_W, SCREEN_H, 0x08, 0x0C, 0x28));
    packet2_update(gif, draw_enable_tests(gif->next, 0, &zbuf));
    packet2_update(gif, draw_finish(gif->next));
    dma_channel_send_packet2(gif, DMA_CHANNEL_GIF, 1);
    draw_wait_finish();
    { u32 bw1; __asm__ volatile("mfc0 %0, $9" : "=r"(bw1)); render_begin_wait = bw1 - bw0; }
    packet2_reset(vif[cur], 0);
    nmats = 0;
    tris_drawn = 0;
    light_sent = NULL; /* its colours may have changed (blends): sent again on the first lit draw */
    cur_tex = -1;
}

static void build_chain(Model *md);

/* Texture cache: the textures live in EE RAM (the packs) and come to GS memory when a model about to be drawn uses
 * them. GS memory above the frame buffers is a run of 64-word blocks, first fit; when it is full, the least recently
 * used texture whose last VIF chain has completed goes (nothing in flight still reads its blocks or REFs its state
 * packet; the GS draws and uploads in order). If every resident texture is still in use, the chain built so far is
 * sent and waited for, which frees them all. Uploads go on PATH3 (GIF DMA) right before the VIF chain that draws
 * with them. Each texture keeps its GS state packet; TEX0 is rewritten when it moves, and the prebuilt chains REF
 * the packet, so they follow. */
#define TEX_BLOCKS ((VRAM_WORDS >> 6) + 1)
static const Scene *tex_scene;
static int tex_block[SCENE_MAX_TEX], tex_nblk[SCENE_MAX_TEX];
static u32 tex_last[SCENE_MAX_TEX];
static s16 *block_tex;              /* owner of each block, -1 free */
static int block_first, block_end;   /* the cache's blocks */
static u32 chain_seq = 1, done_seq, tex_misses, tex_overflow, tex_syncs; /* chain being built, last one completed */
#define MAX_UPLOADS 96
static int uploads[MAX_UPLOADS], nuploads;
static qword_t *upload_chain;

/* PSMT4: pixels in 128-pixel rows, then the 16-entry CLUT (one block). PSMT8 (skies): the 256-entry CLUT first (16 x
 * 16 PSMCT32: 4 blocks, 4-aligned so they make one square), then the pixels in rows of 128 or 256. */
#define T8(t) ((t)->psm == GS_PSM_8)
#define TBW(t) (T8(t) && (t)->w > 128 ? 4 : 2)
/* Blocks a w x h image spans from its base block in a buffer `bw` pixels wide: the GS swizzles the blocks of a page
 * (PSMT4: 128 x 128 pages of 32 x 16 blocks; PSMT8: 128 x 64 pages of 16 x 16), so a strip shorter than a page
 * reaches further than its size says (128 x 32 PSMT4 touches blocks 0-3 and 8-11). Highest block touched + 1. */
static const unsigned char blk4[8][4] = {{0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
                                         {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
static const unsigned char blk8[4][8] = {{0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15},
                                         {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31}};
static int footprint(int t8, int w, int h, int bw)
{
    int bkw = t8 ? 16 : 32, bkh = 16, pw = 128, ph = t8 ? 64 : 128, top = 0;
    for (int y = 0; y < h; y += bkh)
        for (int x = 0; x < w; x += bkw) {
            int page = (y / ph) * (bw / pw) + x / pw;
            int b = t8 ? blk8[(y % ph) / bkh][(x % pw) / bkw] : blk4[(y % ph) / bkh][(x % pw) / bkw];
            if (page * 32 + b + 1 > top) top = page * 32 + b + 1;
        }
    return top;
}

/* PSMT4 level k (0 = base): in a 128-pixel-wide buffer; the mips follow the base, the CLUT comes last */
static int lvl_blocks(const TexEntry *t, int k)
{
    return footprint(0, t->w >> k, t->h >> k, 128);
}

static int blocks_of(const TexEntry *t)
{
    if (T8(t))
        return 4 + footprint(1, t->w, t->h, 64 * TBW(t));
    int n = 1;
    for (int k = 0; k <= t->nmip; k++)
        n += lvl_blocks(t, k);
    return n;
}

static void tex_free(int t)
{
    for (int b = tex_block[t]; b >= 0 && b < tex_block[t] + tex_nblk[t]; b++)
        block_tex[b] = -1;
    tex_block[t] = -1;
}

static u32 block_rsv;
static int tex_alloc(int n, int align)
{
    for (int b = block_first + block_rsv, run = 0; b < block_end; b++) {
        run = block_tex[b] < 0 && (run || b % align == 0) ? run + 1 : 0;
        if (run == n)
            return b - n + 1;
    }
    return -1;
}

static void flush_uploads(void)
{
    if (!nuploads)
        return;
    qword_t *q = upload_chain;
    for (int i = 0; i < nuploads; i++) {
        int t = uploads[i];
        const TexEntry *e = tex_scene->tex[t].e;
        if (T8(e)) {
            q = draw_texture_transfer(q, (void *)(tex_scene->tex[t].base + e->pix_off), e->w, e->h, GS_PSM_8,
                                      (tex_block[t] + 4) * 64, 64 * TBW(e));
            q = draw_texture_transfer(q, (void *)(tex_scene->tex[t].base + e->clut_off), 16, 16, GS_PSM_32,
                                      tex_block[t] * 64, 64);
            continue;
        }
        int blk = tex_block[t], clut = (tex_block[t] + tex_nblk[t] - 1) * 64;
        const u8 *pix = tex_scene->tex[t].base + e->pix_off;
        for (int k = 0; k <= e->nmip; k++) {
            int w = e->w >> k, h = e->h >> k;
            q = draw_texture_transfer(q, (void *)pix, w, h, GS_PSM_4, blk * 64, 128);
            pix += (w * h / 2 + 15) & ~15;
            blk += lvl_blocks(e, k);
        }
        q = draw_texture_transfer(q, (void *)(tex_scene->tex[t].base + e->clut_off), 8, 2, GS_PSM_32, clut, 64);
    }
    q = draw_texture_flush(q);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_chain(DMA_CHANNEL_GIF, upload_chain, q - upload_chain, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    nuploads = 0;
}

static void flush_vif(void);

/* Make texture t resident for the chain being built (upload queued for the next flush). Returns 1 if it had to
 * send and wait for the chain built so far (textures marked before that may have been evicted since). */
static int tex_need(u32 t)
{
    if (t == NO_TEX)
        return 0;
    tex_last[t] = chain_seq;
    if (tex_block[t] >= 0)
        return 0;
    int synced = 0;
    if (nuploads == MAX_UPLOADS) {
        flush_vif();                 /* sends the queued uploads, then what was drawn so far */
        tex_last[t] = chain_seq;
    }
    const TexEntry *e = tex_scene->tex[t].e;
    int n = tex_nblk[t], b;
    while ((b = tex_alloc(n, T8(e) ? 32 : 1)) < 0) { /* PSMT8 from a page's start: from a 4-block step, the font's
                                                     * 256 x 256 glyph sheet and the title's copyright under it overwrote
                                                     * each other (PCSX2 hardware and software alike) */
        /* no room: evict the least recently used texture whose chain has completed */
        int lru = -1;
        for (u32 i = 0; i < tex_scene->ntex; i++)
            if (tex_block[i] >= 0 && tex_last[i] <= done_seq && (lru < 0 || tex_last[i] < tex_last[lru]))
                lru = i;
        if (lru >= 0) {
            tex_free(lru);
            continue;
        }
        if (synced) {
            tex_overflow++;          /* larger than the whole cache: drawn with stale texels */
            return 1;
        }
        flush_vif();                 /* all in use: send what is built, wait for it, then they can go */
        dma_channel_wait(DMA_CHANNEL_VIF1, 0);
        done_seq = chain_seq - 1;
        tex_last[t] = chain_seq;
        tex_syncs++;
        synced = 1;
    }
    tex_block[t] = b;
    for (int i = b; i < b + n; i++)
        block_tex[i] = t;
    qword_t *p = tex_packets + TEXP * t;
    p[1].dw[0] = T8(e) ? GS_SET_TEX0(b + 4, TBW(e), GS_PSM_8, e->tw, e->th, 1, 0, b, GS_PSM_32, 0, 0, 1)
                       : GS_SET_TEX0(b, 2, GS_PSM_4, e->tw, e->th, 1, 0, b + n - 1, GS_PSM_32, 0, 0, 1);
    if (!T8(e) && e->nmip) { /* MIPTBP1: levels 1-3 after the base, rows of 128 (TBW 2) */
        u64 b1 = b + lvl_blocks(e, 0), b2 = b1 + lvl_blocks(e, 1), b3 = b2 + lvl_blocks(e, 2);
        p[7].dw[0] = b1 | 2ULL << 14 | b2 << 20 | 2ULL << 34 | b3 << 40 | 2ULL << 54;
    }
    uploads[nuploads++] = t;
    tex_misses++;
    return synced;
}

/* every texture of a model resident for its CALL; after a sync the earlier ones are checked again */
static void model_textures(const Model *md)
{
    for (int pass = 0; pass < 2; pass++) {
        int synced = 0;
        for (u32 g = 0; g < md->vu_ngroups; g++)
            synced |= tex_need(md->vu[g].tex);
        if (!synced)
            return;
    }
}

/* texture i's cache entry and GS state packet from its TexEntry (not resident yet) */
static void tex_setup(u32 i)
{
    const TexEntry *t = tex_scene->tex[i].e;
    tex_block[i] = -1;
    tex_nblk[i] = blocks_of(t);
    tex_last[i] = 0;
    qword_t *p = tex_packets + TEXP * i;
    p[0].dw[0] = GIF_SET_TAG(TEXP - 1, 1, 0, 0, GIF_FLG_PACKED, 1);
    p[0].dw[1] = GIF_REG_AD;
    p[1].dw[1] = GS_REG_TEX0;    /* TEX0 itself is written when the texture gets its blocks (tex_need) */
    /* no mips: fixed LOD 0, bilinear; mips: LOD = log2(depth) + K, bilinear in the nearest level (MMIN 4) */
    p[2].dw[0] = t->nmip ? (u64)t->nmip << 2 | 1 << 5 | 5 << 6 | (u64)((t->lod_k + LOD_BIAS) & 0xFFF) << 32
                         : GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0);
    p[2].dw[1] = GS_REG_TEX1;
    p[7].dw[0] = 0; /* MIPTBP1 itself is written with TEX0 (tex_need) */
    p[7].dw[1] = 0x34; /* MIPTBP1_1 */
    p[3].dw[0] = GS_SET_CLAMP(t->clamp_s, t->clamp_t, 0, 0, 0, 0);
    p[3].dw[1] = GS_REG_CLAMP;
    /* texels under half alpha are cut out; a blended material blends them instead (blend_state's TEST) */
    p[4].dw[0] = t->alpha ? GS_SET_TEST(1, ATEST_METHOD_GREATER_EQUAL, 0x40, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL)
                          : test_z;
    p[4].dw[1] = GS_REG_TEST;
    p[5].dw[0] = GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    p[5].dw[1] = GS_REG_ALPHA;
    p[6].dw[0] = GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0);
    p[6].dw[1] = GS_REG_ZBUF;
}

/* menu.c (the map's GalaxyDetail picture, read on demand): scene texture t's pixels now from `base` as `e` says */
void render_set_texture(u32 t, const TexEntry *e, const u8 *base)
{
    if (!tex_scene || t >= tex_scene->ntex) return;
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    if (tex_block[t] >= 0) tex_free(t);
    tex_scene->tex[t].e = e;
    tex_scene->tex[t].base = base;
    tex_setup(t);
    FlushCache(0);
    cur_tex = -1;
}

void render_load_textures(const Scene *s, u32 first_tex, u32 first_model)
{
    render_flip_wait(); /* the last frame may still draw with these textures */
    u32 ntex = s->ntex;
    tex_scene = s;
    if (!tex_packets) { /* fixed slots (render_init: below main.c's scene mark): prebuilt chains point into them; the
                         * NO_TEX packet sits after the last */
        tex_packets = mem_alloc(MEM_RENDER, (TEXP * SCENE_MAX_TEX + 3) * 16);
        block_tex = mem_alloc(MEM_RENDER, TEX_BLOCKS * sizeof(s16));
        upload_chain = mem_alloc(MEM_RENDER, (MAX_UPLOADS * 5 * 16 + 4) * 16); /* base, 3 mips, CLUT */
        block_first = graph_vram_allocate(64, 1, GS_PSM_32, GRAPH_ALIGN_BLOCK) / 64; /* first block above libgraph's */
        block_end = VRAM_WORDS / 64;
        for (int b = 0; b < TEX_BLOCKS; b++)
            block_tex[b] = -1;
        qword_t *p = NO_TEX_PACKET;
        p[0].dw[0] = GIF_SET_TAG(2, 1, 0, 0, GIF_FLG_PACKED, 1);
        p[0].dw[1] = GIF_REG_AD;
        p[1].dw[0] = test_z;
        p[1].dw[1] = GS_REG_TEST;
        p[2].dw[0] = GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0);
        p[2].dw[1] = GS_REG_ZBUF;
    }
    u32 need = 0;
    for (u32 i = first_tex; i < ntex; i++) {
        tex_setup(i);
        need += tex_nblk[i];
    }
    FlushCache(0);
    vram_tex = need * 64;
    printf("render: %u textures (%u KB if all resident), texture cache %u KB" "\n", ntex, need * 256 / 1024,
           (block_end - block_first) * 256 / 1024);
    for (u32 i = first_model; i < s->nmodels; i++)
        if (!s->models[i].skin && s->models[i].vu_ngroups)
            build_chain(&s->models[i]);
}

void render_release(Scene *s, u32 first_tex, u32 first_model)
{
    render_flip_wait(); /* the last frame may still draw with these textures */
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    for (u32 i = first_model; i < s->nmodels; i++)
        for (int x = 0; x < 2; x++) {
            void **c = x ? &s->models[i].chain_xlu : &s->models[i].chain;
            if (!*c) continue;
            packet2_t *pk = *c;
            mem_charge(MEM_RENDER, -(u32)(pk->max_qwords_count * 16));
            packet2_free(pk);
            *c = NULL;
        }
    for (u32 t = first_tex; t < s->ntex; t++)
        if (tex_block[t] >= 0)
            tex_free(t);
    cur_tex = -1;
}

/* Switch texture state in the VIF chain: FLUSH waits for VU1 and PATH1, DIRECT sends the A+D packet on PATH2. */
static void emit_tex(packet2_t *pk, u32 t)
{
    const qword_t *p = t == NO_TEX ? NO_TEX_PACKET : tex_packets + TEXP * t;
    u32 n = t == NO_TEX ? 3 : TEXP;
    packet2_chain_ref(pk, p, n, 0, 0, 0);
    packet2_vif_flush(pk, 0);
    packet2_vif_open_direct(pk, 0);
    packet2_vif_close_direct_manual(pk, n);
}

static void set_tex(u32 t)
{
    tex_need(t);
    if ((int)t == cur_tex)
        return;
    cur_tex = t;
    emit_tex(vif[cur], t);
}

/* UNPACK of n vectors from `data` in a format narrower than a qword (REF tag, the VIF codes ride in the tag). The
 * REF moves whole qwords; the bytes past the vectors are the zero padding of the array (VIF NOPs) or, between
 * batches, never there because BATCH is a multiple of 12. */
static void unpack_narrow_to(packet2_t *pk, const void *data, u32 bytes, u32 dest, u32 n, enum UnpackMode mode, u8 usn)
{
    packet2_chain_ref(pk, data, (bytes + 15) / 16, 0, 0, 0);
    packet2_vif_stcycl(pk, 0, 0x0101, 0);
    packet2_vif_open_unpack(pk, mode, dest, 1, 0, usn, 0);
    packet2_vif_close_unpack_manual(pk, n);
}

/* Prebuilt chain of a static model: every batch (header, positions, colours, UVs, MSCAL) and every texture
 * change, ending with RET. Drawing an instance is then a FLUSHE, the instance matrix to VU mem 1..4 and a CALL:
 * the EE no longer walks the batches each frame (milestone 2 flagged 4.3-5.9 ms of EE draw time). */
/* the batch header from a template (HDR / HDRS) with w = unique vertex count, unpacked inline to VU mem 0 */
static void add_header(packet2_t *pk, const qword_t *tmpl, u32 nu)
{
    packet2_utils_vu_open_unpack(pk, 0, 1);
    packet2_add_2x_s64(pk, tmpl[0].dw[0], (s64)(((u64)nu << 32) | (u32)tmpl[0].sw[2]));
    packet2_add_2x_s64(pk, tmpl[1].dw[0], tmpl[1].dw[1]);
    packet2_utils_vu_close_unpack(pk);
}

/* one indexed batch from `base` on (after the matrices): positions, UVs, normals if lit, colours; advances *p past
 * its data */
static void add_batch(packet2_t *pk, const u8 **p, u32 ne, u32 nu, int textured, int lit, u32 base)
{
    const u8 *d = *p;
    unpack_narrow_to(pk, d, 8 * nu, base, nu, P2_UNPACK_V4_16, 0);
    d += (8 * nu + 15) & ~15u;
    if (textured) {
        unpack_narrow_to(pk, d, 4 * nu, base + nu, nu, P2_UNPACK_V2_16, 0);
        d += (4 * nu + 15) & ~15u;
    }
    if (lit) {
        unpack_narrow_to(pk, d, 4 * nu, base + 2 * nu, nu, P2_UNPACK_V4_8, 0);
        d += (4 * nu + 15) & ~15u;
    }
    unpack_narrow_to(pk, d, 4 * ne, base + (lit ? 3 : 2) * nu, ne, P2_UNPACK_V4_8, 1);
    d += (4 * ne + 15) & ~15u;
    *p = d;
}

/* one skinned lit strip batch (VU1SkinLit), after its matrices: positions to TOP+72, UVs +132, normals +192, colours
 * +252 (vu1_tex.vsm's fixed places) */
static void add_skin_strip_batch(packet2_t *pk, const u8 **p, u32 n, int textured)
{
    const u8 *d = *p;
    unpack_narrow_to(pk, d, 8 * n, 72, n, P2_UNPACK_V4_16, 0);
    d += (8 * n + 15) & ~15u;
    if (textured) {
        unpack_narrow_to(pk, d, 4 * n, 132, n, P2_UNPACK_V2_16, 0);
        d += (4 * n + 15) & ~15u;
    }
    unpack_narrow_to(pk, d, 4 * n, 192, n, P2_UNPACK_V4_8, 0);
    d += (4 * n + 15) & ~15u;
    unpack_narrow_to(pk, d, 4 * n, 252, n, P2_UNPACK_V4_8, 1);
    d += (4 * n + 15) & ~15u;
    *p = d;
}

/* one strip batch (VU1Strip): positions to TOP+2, UVs to +82, colours to +162 (vu1_tex.vsm's fixed places) */
static void add_strip_batch(packet2_t *pk, const u8 **p, u32 n, int textured)
{
    const u8 *d = *p;
    unpack_narrow_to(pk, d, 8 * n, 2, n, P2_UNPACK_V4_16, 0);
    d += (8 * n + 15) & ~15u;
    if (textured) {
        unpack_narrow_to(pk, d, 4 * n, 82, n, P2_UNPACK_V2_16, 0);
        d += (4 * n + 15) & ~15u;
    }
    unpack_narrow_to(pk, d, 4 * n, 162, n, P2_UNPACK_V4_8, 1);
    d += (4 * n + 15) & ~15u;
    *p = d;
}

/* header mode of a group: bit 0 textured (PRIM TME), bit 1 blended material (PRIM ABE, drawn in the late pass) */
static int group_mode(const VuGroup *vg)
{
    return (vg->tex != NO_TEX) | (vg->blend ? 2 : 0);
}

/* GS state of a blended material, after its texture packet: ALPHA (Cs - Cd) A + Cd, or Cs A + Cd when additive, with
 * A the texture alpha when it has one, else the material alpha (FIX, 16 steps); ZBUF writes depth only if the
 * material does; TEST without the alpha cut. Prebuilt per (additive, z write, texture alpha, alpha / 16). */
#define BLEND_STATES (5 * 2 * 2 * 16)
static qword_t *blend_states;

static const qword_t *blend_state(const VuGroup *vg)
{
    int as = vg->tex != NO_TEX && tex_scene->tex[vg->tex].e->alpha;
    return blend_states + 4 * ((((vg->blend >= 2 ? vg->blend - 1 : 0) * 2 + !!vg->zwrite) * 2 + as) * 16 + (vg->alpha >> 4));
}

static void init_blend_states(void)
{
    blend_states = mem_alloc(MEM_RENDER, BLEND_STATES * 4 * 16);
    for (int i = 0; i < BLEND_STATES; i++) {
        int kind = i >> 6, zw = i >> 5 & 1, as = i >> 4 & 1, a = (i & 15) * 0x11 >> 1; /* FIX: 0..0x80 */
        /* kind 3 (blend 4): Cd x FIX + Cs, the frame buffer through a tint's mean plus the tint (render_group_tint) */
        qword_t *p = blend_states + 4 * i;
        p[0].dw[0] = GIF_SET_TAG(3, 1, 0, 0, GIF_FLG_PACKED, 1);
        p[0].dw[1] = GIF_REG_AD;
        p[1].dw[0] = kind == 2 ? GS_SET_ALPHA(1, 2, 2, 2, a) /* Cd x FIX */
                   : kind == 3 ? GS_SET_ALPHA(1, 2, 2, 0, a) /* Cd x FIX + Cs */
                   : kind == 4 ? GS_SET_ALPHA(1, 2, 2, 1, a) /* Cd x FIX + Cd: blend 5, the frame buffer brightened */
                               : GS_SET_ALPHA(0, kind ? 2 : 1, as ? 0 : 2, 1, a);
        p[1].dw[1] = GS_REG_ALPHA;
        p[2].dw[0] = GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, !zw);
        p[2].dw[1] = GS_REG_ZBUF;
        p[3].dw[0] = GS_SET_TEST(1, ATEST_METHOD_GREATER_EQUAL, 0x01, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL);
        p[3].dw[1] = GS_REG_TEST;
    }
}

/* a group's texture, then its blend state when its material blends */
static void emit_group(packet2_t *pk, const VuGroup *vg)
{
    emit_tex(pk, vg->tex);
    if (!vg->blend)
        return;
    packet2_chain_ref(pk, blend_state(vg), 4, 0, 0, 0);
    packet2_vif_flush(pk, 0);
    packet2_vif_open_direct(pk, 0);
    packet2_vif_close_direct_manual(pk, 4);
}

/* a UV matrix to VU mem 8..10 (columns s, t, 1): FLUSHE first, the running batch may still be starting (it loads them) */
static void add_uvm(packet2_t *pk, const float *m)
{
    packet2_chain_open_cnt(pk, 0, 0, 0);
    packet2_vif_flushe(pk, 0);
    packet2_vif_nop(pk, 0);
    packet2_chain_close_tag(pk);
    packet2_utils_vu_add_unpack_data(pk, 8, (void *)m, 3, 0);
}

/* an environment-mapped group's matrix (render_env): columns to VU mem 8..10, its constant to 15 */
static void add_envm(packet2_t *pk, const float *m)
{
    add_uvm(pk, m);
    packet2_utils_vu_add_unpack_data(pk, 15, (void *)(m + 12), 1, 0);
}

/* VU1Env's matrix per environment-mapped group, after the projection is set (render_set_fovy / shake): the normal
 * through the clip matrix's columns (x 127 x the model's quantisation step) gives (p0 x + p8 z, p5 y + p9 z, ., z) of
 * the view normal (z away from the viewer); undone here and composed with the group's texgen (GX: z to the viewer).
 * ponytail: an instance's own scale scales its normals (the UVs spread with it) */
void render_env(const Scene *s)
{
    for (u32 i = 0; i < s->nmodels; i++) {
        const Model *md = &s->models[i];
        for (u32 g = 0; g < md->vu_ngroups; g++) {
            const VuGroup *vg = &md->vu[g];
            if (!vg->env) continue;
            float k = md->qscale * 127.0f, *u = vg->uvm;
            for (int r = 0; r < 3; r++) {
                float ex = vg->env[r], ey = vg->env[3 + r], ez = vg->env[6 + r];
                u[r] = ex / (proj[0] * k);
                u[4 + r] = ey / (proj[5] * k);
                u[8 + r] = (-ex * proj[8] / proj[0] - ey * proj[9] / proj[5] - ez) / k;
                u[12 + r] = vg->env[9 + r];
            }
            u[3] = u[7] = u[11] = u[15] = 0.0f;
        }
    }
}

/* GX lit channel (LightFunction::loadActorLightInfo: lights 0 and 1, attenuation off, the ambient register): colours
 * to VU mem 11..14 x 2 / 1 (vu1_tex.vsm doubles; the converter stores colours x gain / 2), after a FLUSHE */
static void send_light(const RenderLight *l)
{
    if (!l || l == light_sent)
        return;
    for (int k = 0; k < 3; k++) {
        light_qw[k] = 2.0f * l->col[0][k];
        light_qw[4 + k] = 2.0f * l->col[1][k];
        light_qw[8 + k] = 2.0f * l->amb[k];
        light_qw[12 + k] = 2.0f;
    }
    light_qw[15] = 255.0f;
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flushe(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_chain_close_tag(vif[cur]);
    packet2_utils_vu_open_unpack(vif[cur], 11, 0);
    for (int i = 0; i < 4; i++)
        packet2_add_2x_s64(vif[cur], ((s64 *)light_qw)[2 * i], ((s64 *)light_qw)[2 * i + 1]);
    packet2_utils_vu_close_unpack(vif[cur]);
    light_sent = l;
}

void render_light(const RenderLight *l) { light = l; }

/* the light matrix of a matrix `w` (local -> world) at m[16..27]: per light the direction from the instance to the
 * light (world), into w's local space (w^T d, normalised: w is a rotation times a scale), x 1 / 127 (the normals'
 * scale); columns x, y, z of (light 0, light 1). ponytail: one direction per instance, not per vertex */
/* the light matrix cleared by stores (memset's call costs more for 12 floats) */
static inline void zero12(float *m)
{
    m[0] = m[1] = m[2] = m[3] = m[4] = m[5] = m[6] = m[7] = m[8] = m[9] = m[10] = m[11] = 0.0f;
}
static void light_matrix(float *m, const float w[16], const float at[3])
{
    zero12(m + 16);
    if (!light)
        return;
    for (int k = 0; k < 2; k++) {
        float d[3] = {light->pos[k][0] - at[0], light->pos[k][1] - at[1], light->pos[k][2] - at[2]}, v[3];
        for (int i = 0; i < 3; i++)
            v[i] = w[4 * i] * d[0] + w[4 * i + 1] * d[1] + w[4 * i + 2] * d[2];
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 1e-6f)
            continue;
        float inv = 1.0f / (l * 127.0f);
        for (int i = 0; i < 3; i++)
            m[16 + 4 * i + k] = v[i] * inv;
    }
}
/* light_matrix of world x drw for the draw matrices of one skinned draw: (world drw)^T d = drw^T (world^T d), so the
 * lights' directions go into world's frame once (dl) and each draw matrix (affine) takes them on from there */
static void light_dirs(float dl[2][3], const float w[16], const float at[3])
{
    for (int k = 0; k < 2; k++) {
        float d[3] = {light->pos[k][0] - at[0], light->pos[k][1] - at[1], light->pos[k][2] - at[2]};
        for (int i = 0; i < 3; i++)
            dl[k][i] = w[4 * i] * d[0] + w[4 * i + 1] * d[1] + w[4 * i + 2] * d[2];
    }
}
static void light_matrix_drw(float *m, const float drw[16], const float dl[2][3])
{
    zero12(m + 16);
    for (int k = 0; k < 2; k++) {
        float v[3];
        for (int i = 0; i < 3; i++)
            v[i] = drw[4 * i] * dl[k][0] + drw[4 * i + 1] * dl[k][1] + drw[4 * i + 2] * dl[k][2];
        float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l < 1e-6f)
            continue;
        float inv = 1.0f / (l * 127.0f);
        for (int i = 0; i < 3; i++)
            m[16 + 4 * i + k] = v[i] * inv;
    }
}

/* the prebuilt chain of a model's opaque groups (xlu 0) or of its blended ones (xlu 1); NULL when it has none. A BTK
 * group's batches run between its own UV matrix (VuGroup.uvm, refreshed each frame: game_draw, anim_btk) and the
 * identity again */
static packet2_t *build_chain_pass(Model *md, int xlu)
{
    u32 batches = 0, groups = 0, btks = 0;
    for (u32 g = 0; g < md->vu_ngroups; g++)
        if ((group_mode(&md->vu[g]) >= 2) == xlu) {
            batches += md->vu[g].nbatch;
            groups++;
            btks += md->vu[g].uvm != NULL;
        }
    if (!groups)
        return NULL;
    u32 qw = batches * 9 + groups * 4 + btks * 12 + 4;
    packet2_t *pk = packet2_create(qw, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    mem_charge(MEM_RENDER, qw * 16);
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        const VuGroup *vg = &md->vu[g];
        int mode = group_mode(vg);
        if ((mode >= 2) != xlu) continue;
        emit_group(pk, vg);
        if (vg->env)
            add_envm(pk, vg->uvm);
        else if (vg->uvm)
            add_uvm(pk, vg->uvm);
        const u8 *d = vg->data;
        for (u32 b = 0; b < vg->nbatch; b++) {
            u32 ne = vg->batch[2 * b], nu = vg->batch[2 * b + 1];
            if (vg->strip == 3) { /* VU1EnvStrip: positions, normals where VU1Strip has UVs, colours */
                add_header(pk, HSTRIP(mode, ne), 0);
                unpack_narrow_to(pk, d, 8 * ne, 2, ne, P2_UNPACK_V4_16, 0);
                d += (8 * ne + 15) & ~15u;
                unpack_narrow_to(pk, d, 4 * ne, 82, ne, P2_UNPACK_V4_8, 0);
                d += (4 * ne + 15) & ~15u;
                unpack_narrow_to(pk, d, 4 * ne, 162, ne, P2_UNPACK_V4_8, 1);
                d += (4 * ne + 15) & ~15u;
            } else if (vg->strip) {
                add_header(pk, HSTRIP(mode, ne), 0);
                add_strip_batch(pk, &d, ne, vg->tex != NO_TEX);
            } else {
                add_header(pk, HDRS(mode, ne), nu);
                add_batch(pk, &d, ne, nu, vg->tex != NO_TEX, vg->lit, 2);
            }
            packet2_utils_vu_add_start_program(pk, PROGRAM(vg));
        }
        if (vg->uvm)
            add_uvm(pk, uv_ident);
    }
    packet2_chain_ret(pk, 0, 0);
    packet2_vif_nop(pk, 0);
    packet2_vif_nop(pk, 0);
    packet2_chain_close_tag(pk);
    return pk;
}

static void build_chain(Model *md)
{
    md->chain = build_chain_pass(md, 0);
    md->chain_xlu = build_chain_pass(md, 1);
}

static u32 vif_wait; /* EE cycles spent waiting for the previous chain this frame (render_wait_cycles) */
u32 render_wait_cycles(void) { u32 w = vif_wait; vif_wait = 0; return w; }
static void flush_vif(void)
{
    /* FLUSH: the chain only ends once VU1 has finished and PATH1 has delivered its last XGKICK, so whatever is
     * sent on PATH3 afterwards (the HUD) lands on top */
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flush(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0); /* two VIF codes keep the CNT tag 16-byte aligned */
    packet2_chain_close_tag(vif[cur]);
    packet2_utils_vu_add_end_tag(vif[cur]);
    u32 w0, w1;
    asm volatile("mfc0 %0, $9" : "=r"(w0)); /* the COP0 Count (game.h cycles) */
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    asm volatile("mfc0 %0, $9" : "=r"(w1));
    vif_wait += w1 - w0;
    done_seq = chain_seq - 1;         /* the previous chain has completed (it ends with FLUSH) */
    flush_uploads();
    dma_channel_send_packet2(vif[cur], DMA_CHANNEL_VIF1, 1);
    chain_seq++;
    cur ^= 1;
    packet2_reset(vif[cur], 0);
}

/* m * diag(s, s, s, 1): the model's s16 positions are in steps of s */
static void scale_cols(float m[16], float s)
{
    for (int i = 0; i < 12; i++)
        m[i] *= s;
}

/* Draw a model: drw = NULL for a static model (one matrix, proj * view * world), else the draw matrices of a
 * skinned instance (anim.c), each group getting its SKIN table's matrices premultiplied by proj * view * world. */
/* a prebuilt chain for one instance: FLUSHE (the previous instance stops reading VU mem 1..7), its matrix and light
 * matrix, CALL */
static void call_chain(void *chain, float *m)
{
    if (packet2_get_qw_count(vif[cur]) + 16 > PACKET_QW)
        flush_vif();
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flushe(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_chain_close_tag(vif[cur]);
    packet2_utils_vu_add_unpack_data(vif[cur], 1, m, 7, 0);
    packet2_chain_call(vif[cur], ((packet2_t *)chain)->base, 0, 0, 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_chain_close_tag(vif[cur]);
    cur_tex = -1; /* the chain changed the texture state */
}

/* blended instances of the frame, drawn after everything opaque (render_end), farthest first. ponytail: sorted per
 * instance, not per triangle; skinned models blend in place */
#define MAX_XLU 128
static struct { const Model *md; float *m; float depth; const RenderLight *light; } xlu[MAX_XLU];
static int nxlu;
static int bloom_drawing; /* bloom_pass: blended groups drawn at once */

static inline u32 cy_(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
u32 prof_draw, prof_dcalls, prof_dculled; /* EE cycles in draw_model, its calls, those culled (profiling) */
u32 prof_x[8]; /* ad hoc probes (main.c prints them in the bench) */
static void draw_model_(const Model *md, const float world[16], const float (*drw)[16]);
static int cull_passed; /* render_skinned_visible */
static void draw_model(const Model *md, const float world[16], const float (*drw)[16])
{
    u32 c0 = cy_();
    prof_dcalls++;
    draw_model_(md, world, drw);
    u32 d = cy_() - c0;
    prof_draw += d;
    prof_x[drw ? 1 : 0] += d;
}
/* One skinned (or unchained) batch written straight into the VIF chain: the very qwords add_header,
 * packet2_utils_vu_add_unpack_data, add_skin_strip_batch / add_batch and packet2_utils_vu_add_start_program build
 * (BENCH builds check them against those), without packet2's per-code bookkeeping (~600 EE cycles a batch, ~1000
 * batches a frame in Good Egg). Room: draw_model_'s check before its groups. */
#define FQ_TAG(qwc, id, addr) ((u64)(qwc) | (u64)(id) << 28 | (u64)((u32)(addr) & 0x7FFFFFFF) << 32)
#define FQ_STCYCL MAKE_VIF_CODE(0x0101, 0, P2_VIF_STCYCL, 0)
#define FQ_UNPACK(mode, dest, usn, n) MAKE_VIF_CODE((dest) | (u32)(usn) << 14 | 1u << 15, (n) & 0xFF, (mode) | 0x60, 0)
static inline u64 *fq_ref(u64 *q, const void *data, u32 bytes, u32 dest, u32 n, u32 mode, u32 usn)
{
    q[0] = FQ_TAG((bytes + 15) / 16, P2_DMA_TAG_REF, data);
    q[1] = (u64)FQ_STCYCL | (u64)FQ_UNPACK(mode, dest, usn, n) << 32;
    return q + 2;
}
static void emit_batch(const VuGroup *vg, int mode, u32 ne, u32 nu, const void *gm, u32 m, const u8 **pd, u32 base)
{
    u64 *q = (u64 *)vif[cur]->next;
    const qword_t *h = vg->strip == 2 ? HSTRIP(mode, ne) : HDR(mode, m, ne);
    u32 hn = vg->strip == 2 ? 0 : nu;
    int tex = vg->tex != NO_TEX;
    q[0] = FQ_TAG(2, P2_DMA_TAG_CNT, 0);
    q[1] = (u64)FQ_STCYCL | (u64)FQ_UNPACK(P2_UNPACK_V4_32, 0, 1, 2) << 32;
    q[2] = h[0].dw[0];
    q[3] = (u64)hn << 32 | (u32)h[0].sw[2];
    q[4] = h[1].dw[0];
    q[5] = h[1].dw[1];
    q = fq_ref(q + 6, gm, 16 * 7 * m, 2, 7 * m, P2_UNPACK_V4_32, 1);
    const u8 *d = *pd;
    if (vg->strip == 2) {
        q = fq_ref(q, d, 8 * ne, 72, ne, P2_UNPACK_V4_16, 0);
        d += (8 * ne + 15) & ~15u;
        if (tex) {
            q = fq_ref(q, d, 4 * ne, 132, ne, P2_UNPACK_V2_16, 0);
            d += (4 * ne + 15) & ~15u;
        }
        q = fq_ref(q, d, 4 * ne, 192, ne, P2_UNPACK_V4_8, 0);
        d += (4 * ne + 15) & ~15u;
        q = fq_ref(q, d, 4 * ne, 252, ne, P2_UNPACK_V4_8, 1);
        d += (4 * ne + 15) & ~15u;
    } else {
        q = fq_ref(q, d, 8 * nu, base, nu, P2_UNPACK_V4_16, 0);
        d += (8 * nu + 15) & ~15u;
        if (tex) {
            q = fq_ref(q, d, 4 * nu, base + nu, nu, P2_UNPACK_V2_16, 0);
            d += (4 * nu + 15) & ~15u;
        }
        if (vg->lit) {
            q = fq_ref(q, d, 4 * nu, base + 2 * nu, nu, P2_UNPACK_V4_8, 0);
            d += (4 * nu + 15) & ~15u;
        }
        q = fq_ref(q, d, 4 * ne, base + (vg->lit ? 3 : 2) * nu, ne, P2_UNPACK_V4_8, 1);
        d += (4 * ne + 15) & ~15u;
    }
    q[0] = FQ_TAG(0, P2_DMA_TAG_CNT, 0);
    q[1] = (u64)MAKE_VIF_CODE(0, 0, P2_VIF_FLUSH, 0) | (u64)MAKE_VIF_CODE(PROGRAM(vg), 0, P2_VIF_MSCAL, 0) << 32;
    q += 2;
#ifdef BENCH
    { /* the first batches built again the packet2 way into a zeroed packet: the same qwords */
        static packet2_t *chk;
        static u32 nchk, nbad;
        if (nchk < 4000) {
            if (!chk) chk = packet2_create(32, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
            packet2_reset(chk, 1);
            const u8 *d2 = *pd;
            add_header(chk, h, hn);
            packet2_utils_vu_add_unpack_data(chk, 2, (void *)gm, 7 * m, 1);
            if (vg->strip == 2) add_skin_strip_batch(chk, &d2, ne, tex);
            else add_batch(chk, &d2, ne, nu, tex, vg->lit, base);
            packet2_utils_vu_add_start_program(chk, PROGRAM(vg));
            u32 n = (u32)((u8 *)q - (u8 *)vif[cur]->next);
            if (d2 != d || (u32)((u8 *)chk->next - (u8 *)chk->base) != n || memcmp(chk->base, vif[cur]->next, n)) nbad++;
            if (++nchk == 4000) printf("bench emit_batch: %u of 4000 batches differ from packet2's\n", nbad);
        }
    }
#endif
    *pd = d;
    vif[cur]->next = (qword_t *)q;
}

/* draw_model's culling: view x world, only what the culling and the blended sort read (column 0's length, the
 * translation, into mv); the draw matrix is (proj x view, once a frame) x world */
static int model_culled(const Model *md, const float world[16], float mv[16])
{
    for (int i = 0; i < 3; i++) {
        mv[i] = view[i] * world[0] + view[4 + i] * world[1] + view[8 + i] * world[2] + view[12 + i] * world[3];
        mv[12 + i] = view[i] * world[12] + view[4 + i] * world[13] + view[8 + i] * world[14] + view[12 + i] * world[15];
    }
    float rad = md->radius * sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
    if (mv[14] + rad < NEAR || mv[14] - rad > FAR) /* whole model behind the camera or past the far plane */
        return 1;
    /* side planes through the eye: f x = (w/2) z etc., normals unit (frustum[]); outside by more than rad: culled */
    if (frustum[0] * fabsf(mv[12]) - frustum[1] * mv[14] > rad || frustum[2] * fabsf(mv[13]) - frustum[3] * mv[14] > rad) {
        prof_dculled++;
        return 1;
    }
    return 0;
}
int render_model_visible(const Model *md, const float world[16])
{
    float mv[16];
    return md->vu_ngroups && !model_culled(md, world, mv);
}

#define DRW_CACHE 192 /* a skinned draw's matrices by draw index (anim.h ANIM_MAX_DRW 160) */
static float dcache[DRW_CACHE][MAT_FL] __attribute__((aligned(16)));
static u32 dstamp[DRW_CACHE], dseq = 1;
static void draw_model_(const Model *md, const float world[16], const float (*drw)[16])
{
    if (!md->vu_ngroups)
        return;
    float mv[16], pvw[16] __attribute__((aligned(16)));
    if (!(cull_passed && drw) && model_culled(md, world, mv))
        return;
    if (!pv_ok) { mat_mul(pv, proj, view); pv_ok = 1; }
    mat_mul(pvw, pv, world);
    if (md->lit && packet2_get_qw_count(vif[cur]) + 16 > PACKET_QW)
        flush_vif();
    if (md->lit)
        send_light(light);
    if (!drw && (md->chain || md->chain_xlu)) {
        if (nmats >= MAX_INST)
            return;
        model_textures(md);
        float *m = mats[nmats++];
        memcpy(m, pvw, 64);
        scale_cols(m, md->qscale);
        if (md->lit)
            light_matrix(m, world, world + 12);
        if (md->chain)
            call_chain(md->chain, m);
        if (md->chain_xlu && bloom_drawing)
            call_chain(md->chain_xlu, m);
        else if (md->chain_xlu && nxlu < MAX_XLU) { /* blended groups wait for the end of the frame, far ones first */
            int k = nxlu++;
            for (; k > 0 && xlu[k - 1].depth < mv[14]; k--)
                xlu[k] = xlu[k - 1];
            xlu[k].md = md; xlu[k].m = m; xlu[k].depth = mv[14]; xlu[k].light = light;
        }
        for (u32 g = 0; g < md->vu_ngroups; g++) {
            tris_drawn += md->vu[g].nverts / 3;
            render_vstat[md->vu[g].lit == 2 ? 2 : md->vu[g].lit ? 1 : 0] += md->vu[g].nverts; /* static: 0 tex, 1 lit, 2 env */
        }
        return;
    }
    u32 need = drw ? 0 : 1, batches = 0, btks = 0;
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        batches += md->vu[g].nbatch;
        btks += md->vu[g].uvm != NULL;
        if (drw)
            need += md->skin->groups[g].nmat;
    }
    if (nmats + need > MAX_INST)
        return;
    if (packet2_get_qw_count(vif[cur]) + 8 + batches * 10 + md->vu_ngroups * 2 + btks * 12 > PACKET_QW)
        flush_vif();
    float (*gm)[MAT_FL] = mats + nmats; /* this group's matrices */
    dseq++; /* a new draw: the cached draw matrices are stale */
    u32 m = 1;
    float dl[2][3];
    int have_dl = 0;
    if (!drw) {
        memcpy(gm, pvw, 64);
        scale_cols(gm[0], md->qscale);
        if (md->lit)
            light_matrix(gm[0], world, world + 12);
        nmats++;
    }
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        const VuGroup *vg = &md->vu[g];
        if (vg->strip == 1 || vg->strip == 3) continue; /* static strip groups: their model's prebuilt chain only */
        if (drw && vg->hide) continue; /* a BVA's hidden shape */
        if (drw) {
            const SkGroup *sg = &md->skin->groups[g];
            gm = mats + nmats;
            m = sg->nmat;
            for (u32 i = 0; i < m; i++) { /* each draw matrix once a draw, though several groups use it */
                u32 di = sg->drw[i];
                if (di < DRW_CACHE && dstamp[di] == dseq) { memcpy(gm[i], dcache[di], sizeof gm[i]); continue; }
                mat_mul_scale3(gm[i], pvw, drw[di], md->qscale);
                if (md->lit) {
                    if (!light) zero12(gm[i] + 16);
                    else {
                        if (!have_dl) { light_dirs(dl, world, world + 12); have_dl = 1; }
                        light_matrix_drw(gm[i], drw[di], (const float (*)[3])dl);
                    }
                }
                if (di < DRW_CACHE) { memcpy(dcache[di], gm[i], sizeof gm[i]); dstamp[di] = dseq; }
            }
            nmats += m;
        }
        u32 pb0 = cy_();
        set_tex(vg->tex);
        if (vg->blend) {
            emit_group(vif[cur], vg);
            cur_tex = -1; /* the next group's texture packet puts the opaque state back */
        }
        int mode = group_mode(vg);
        u32 base = 2 + 7 * m;
        const u8 *d = vg->data;
        if (vg->env)
            add_envm(vif[cur], vg->uvm);
        else if (vg->uvm)
            add_uvm(vif[cur], vg->uvm);
        for (u32 b = 0; b < vg->nbatch; b++) {
            u32 ne = vg->batch[2 * b], nu = vg->batch[2 * b + 1];
            /* strip 2 (VU1SkinLit): {n} + strip GIF tag, the matrices, the arrays at fixed places; else indexed */
            emit_batch(vg, mode, ne, nu, gm, m, &d, base);
            tris_drawn += ne / 3;
            render_vstat[3 + (vg->lit == 2 ? 2 : vg->lit ? 1 : 0)] += ne; /* skinned: 3 tex, 4 lit, 5 env */
        }
        if (vg->uvm)
            add_uvm(vif[cur], uv_ident);
        prof_x[drw ? 2 : 3] += cy_() - pb0;
    }
}

/* A drop shadow: a black disc of radius 1 in its XZ plane, blended half dark (ShadowSurface* draw at alpha 0x80), no
 * depth written; built once as a one-batch static model and drawn with the blended instances. */
#define SHADOW_SEG 16
static Model shadow_md;
static VuGroup shadow_vg;
static u16 shadow_batch[2] = {3 * SHADOW_SEG, SHADOW_SEG + 1};
static u8 shadow_data[((8 * (SHADOW_SEG + 1) + 15) & ~15) + 4 * 3 * SHADOW_SEG] __attribute__((aligned(16)));

static void shadow_build(void) /* once, from render_init: its chain stays below the scenes' heap blocks */
{
    {
        s16 *pos = (s16 *)shadow_data; /* centre, then the ring, in steps of 1 / 4096 */
        for (int i = 0; i < SHADOW_SEG; i++) {
            float a = 6.2831853f * i / SHADOW_SEG;
            pos[4 * (i + 1)] = (s16)(cosf(a) * 4096.0f);
            pos[4 * (i + 1) + 2] = (s16)(sinf(a) * 4096.0f);
        }
        u8 *col = shadow_data + ((8 * (SHADOW_SEG + 1) + 15) & ~15);
        for (int i = 0; i < SHADOW_SEG; i++) { /* black, the unique index in byte 3 */
            col[12 * i + 3] = 0;
            col[12 * i + 7] = 1 + i;
            col[12 * i + 11] = 1 + (i + 1) % SHADOW_SEG;
        }
        shadow_vg.nverts = 3 * SHADOW_SEG; shadow_vg.tex = NO_TEX; shadow_vg.nbatch = 1;
        shadow_vg.blend = 1; shadow_vg.zwrite = 0; shadow_vg.alpha = 0x80;
        shadow_vg.batch = shadow_batch; shadow_vg.data = shadow_data;
        shadow_md.name = "shadow"; shadow_md.vu_ngroups = 1; shadow_md.vu = &shadow_vg;
        shadow_md.radius = 1.0f; shadow_md.qscale = 1.0f / 4096.0f;
        FlushCache(0);
        build_chain(&shadow_md);
    }
}

void render_shadow(const float world[16])
{
    draw_model(&shadow_md, world, NULL);
}

/* Depth back to "far" over the whole screen, colour untouched: what was drawn so far (the sky) stays behind
 * everything drawn after. In the VIF1 chain: FLUSH (VU1 and PATH1 done), then DIRECT a sprite with the colour masked,
 * the depth test off and z 0, then the frame mask and depth test back. */
void render_clear_depth(void)
{
    if (packet2_get_qw_count(vif[cur]) + 16 > PACKET_QW)
        flush_vif();
    u32 fb = frame[back].address >> 11, fw = SCREEN_W >> 6;
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flush(vif[cur], 0);
    packet2_vif_open_direct(vif[cur], 0);
    packet2_add_2x_s64(vif[cur], GIF_SET_TAG(9, 1, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
    packet2_add_2x_s64(vif[cur], GS_SET_FRAME(fb, fw, FB_PSM, 0xFFFFFFFF), GS_REG_FRAME);
    packet2_add_2x_s64(vif[cur], GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST);
    packet2_add_2x_s64(vif[cur], GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0), GS_REG_ZBUF);
    packet2_add_2x_s64(vif[cur], GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 0, 0, 0), GS_REG_PRIM);
    packet2_add_2x_s64(vif[cur], GS_SET_RGBAQ(0, 0, 0, 0x80, 0x3F800000), GS_REG_RGBAQ);
    packet2_add_2x_s64(vif[cur], GS_SET_XYZ((2048 - SCREEN_W / 2) << 4, (2048 - SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    packet2_add_2x_s64(vif[cur], GS_SET_XYZ((2048 + SCREEN_W / 2) << 4, (2048 + SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    packet2_add_2x_s64(vif[cur], GS_SET_FRAME(fb, fw, FB_PSM, 0), GS_REG_FRAME);
    packet2_add_2x_s64(vif[cur], test_z, GS_REG_TEST);
    packet2_vif_close_direct_auto(vif[cur]);
    packet2_chain_close_tag(vif[cur]);
    cur_tex = -1; /* TEST changed */
}

void render_model(const Model *md, const float world[16], u8 cr, u8 cg, u8 cb)
{
    (void)cr; (void)cg; (void)cb; /* colours are baked into the VUMD data */
    draw_model(md, world, NULL);
}

/* a model's blend 4 groups (a BRK's C0 over the frame buffer: tools/slice.py brk_c0) to tint rgb: their vertex
 * colours (rgbi bytes 0-2 of each batch, the unindexed layout only) and FIX, the tint's mean */
void render_group_tint(const Model *md, u8 r, u8 g, u8 b)
{
    for (u32 k = 0; k < md->vu_ngroups; k++) {
        VuGroup *vg = &((Model *)md)->vu[k];
        if (vg->blend != 4 || vg->strip) continue;
        vg->alpha = (u8)((r + g + b) / 3);
        u8 *d = (u8 *)vg->data;
        for (u32 bt = 0; bt < vg->nbatch; bt++) {
            u32 ne = vg->batch[2 * bt], nu = vg->batch[2 * bt + 1];
            d += (8 * nu + 15) & ~15u;
            if (vg->tex != NO_TEX) d += (4 * nu + 15) & ~15u;
            if (vg->lit) d += (4 * nu + 15) & ~15u;
            u8 *c = d;
            for (u32 v = 0; v < ne; v++, c += 4) { c[0] = r >> 1; c[1] = g >> 1; c[2] = b >> 1; } /* 0x80 is 1 */
            SyncDCache(d, c);
            d += (4 * ne + 15) & ~15u;
        }
    }
}

void render_skinned(const Model *md, const float world[16], const float (*drw)[16])
{
    draw_model(md, world, drw);
}
/* (cull_passed: draw_model_ skips its culling, a skinned draw reads no mv) */
void render_skinned_visible(const Model *md, const float world[16], const float (*drw)[16])
{
    cull_passed = 1;
    draw_model(md, world, drw);
    cull_passed = 0;
}

/* 3x5 bitmap font as GS sprites (no texture, no VRAM). Digits, upper-case letters and a few signs. */
static const char *glyph_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:%-/ >";
static const u16 glyphs[] = {
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111, 075757, 075711,
    025755, 065656, 034443, 065556, 074647, 074644, 034553, 055755, 072227, 011157,
    055655, 044447, 057555, 065555, 025552, 065644, 025573, 065655, 034716, 072222,
    055557, 055552, 055575, 055255, 055222, 071247, 000020, 002020, 041241, 000700,
    001240, 000000, 042124};
static int text_open;

void render_text(float x, float y, const char *s)
{
    if (!text_open) {
        packet2_reset(gif, 0);
        packet2_update(gif, draw_disable_tests(gif->next, 0, &zbuf)); /* HUD over everything */
        text_open = 1;
    }
    const float px = 2.0f;
    rect_t r;
    r.color.r = r.color.g = r.color.b = 0xFF; r.color.a = 0x80; r.color.q = 1.0f;
    r.v0.z = r.v1.z = Z_MAX;
    for (; *s; s++, x += 4 * px) {
        char ch = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
        const char *at = strchr(glyph_chars, ch);
        if (!at)
            continue;
        u16 gl = glyphs[at - glyph_chars];
        for (int row = 0; row < 5; row++)
            for (int c = 0; c < 3; c++)
                if (gl >> (3 * (4 - row) + (2 - c)) & 1) {
                    r.v0.x = x + c * px - SCREEN_W / 2; r.v0.y = (y + row * px) * YS - SCREEN_H / 2; /* libdraw adds the 2048 offset */
                    r.v1.x = r.v0.x + px; r.v1.y = r.v0.y + px * YS;
                    if (packet2_get_qw_count(gif) < GIF_QW - 96)
                        packet2_update(gif, draw_rect_filled(gif->next, 0, &r));
                }
    }
}

void render_rect(float x, float y, float w, float h, u8 r, u8 g, u8 b)
{
    if (!text_open) {
        packet2_reset(gif, 0);
        packet2_update(gif, draw_disable_tests(gif->next, 0, &zbuf));
        text_open = 1;
    }
    rect_t q;
    q.color.r = r; q.color.g = g; q.color.b = b; q.color.a = 0x80; q.color.q = 1.0f;
    q.v0.z = q.v1.z = Z_MAX;
    q.v0.x = x - SCREEN_W / 2; q.v0.y = y * YS - SCREEN_H / 2;
    q.v1.x = q.v0.x + w; q.v1.y = q.v0.y + h * YS;
    if (packet2_get_qw_count(gif) < GIF_QW - 96)
        packet2_update(gif, draw_rect_filled(gif->next, 0, &q));
}

#include "game.h"

/* Particles (fx.c): camera-facing sprites projected here as the VU1 program would (x = 2048 + f x / z, depth
 * (zndc + 1) * 0xFFFFFF / 2), sent through PATH2 in the chain after what is drawn so far; depth tested, not written.
 * Runs of one texture and blend share a state block; 64 sprites per DIRECT. */
void render_sprites(const Sprite *sp, int n)
{
    for (int i = 0; i < n;) {
        u32 t = sp[i].tex;
        int add = sp[i].add, j = i;
        while (j < n && j - i < 64 && sp[j].tex == t && sp[j].add == add)
            j++;
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 4 + (j - i) * 10 > PACKET_QW) /* state, its texture change */
            flush_vif();
        set_tex(t);
        const TexEntry *e = tex_scene->tex[t].e;
        packet2_chain_open_cnt(vif[cur], 0, 0, 0);
        packet2_vif_flush(vif[cur], 0);
        packet2_vif_open_direct(vif[cur], 0);
        qword_t *tag = (qword_t *)vif[cur]->next;
        packet2_add_2x_s64(vif[cur], 0, GIF_REG_AD);
        packet2_add_2x_s64(vif[cur], add ? GS_SET_ALPHA(0, 2, 0, 1, 0) : GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
        packet2_add_2x_s64(vif[cur], GS_SET_TEST(1, ATEST_METHOD_GREATER, 0, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL), GS_REG_TEST);
        packet2_add_2x_s64(vif[cur], GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 1), GS_REG_ZBUF);
        packet2_add_2x_s64(vif[cur], GS_SET_PRIM(PRIM_SPRITE, 0, 1, 0, 1, 0, 1, 0, 0), GS_REG_PRIM);
        u32 regs = 4;
        for (; i < j; i++) {
            const Sprite *s = &sp[i];
            float xv = view[0] * s->pos[0] + view[4] * s->pos[1] + view[8] * s->pos[2] + view[12];
            float yv = view[1] * s->pos[0] + view[5] * s->pos[1] + view[9] * s->pos[2] + view[13];
            float zv = view[2] * s->pos[0] + view[6] * s->pos[1] + view[10] * s->pos[2] + view[14];
            if (zv < NEAR || zv > FAR)
                continue;
            float k = 2048.0f / zv, sx = 2048.0f + shake_x + proj[0] * xv * k, sy = 2048.0f + shake_y + proj[5] * yv * k;
            float hx = proj[0] * s->half * k, hy = -proj[5] * s->half * k;
            /* wider than the screen (a big emitter's scale, or right at the camera): skipped. Its corners would leave
             * the GS's 0..4095 range and wrap, and a few such sprites stall the GS for seconds (ambient effects) */
            if (hx > SCREEN_W || hy > SCREEN_H)
                continue;
            if (sx + hx < 2048 - SCREEN_W / 2 || sx - hx > 2048 + SCREEN_W / 2 || sy + hy < 2048 - SCREEN_H / 2 ||
                sy - hy > 2048 + SCREEN_H / 2)
                continue;
            u32 z = (u32)(((proj[10] * zv + proj[14]) / zv + 1.0f) * (float)(Z_MAX / 2));
            packet2_add_2x_s64(vif[cur], GS_SET_RGBAQ(s->rgba & 0xFF, s->rgba >> 8 & 0xFF, s->rgba >> 16 & 0xFF, s->rgba >> 24, 0x3F800000), GS_REG_RGBAQ);
            packet2_add_2x_s64(vif[cur], GS_SET_UV(0, 0), GS_REG_UV);
            packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)((sx - hx) * 16), (int)((sy - hy) * 16), z), GS_REG_XYZ2);
            packet2_add_2x_s64(vif[cur], GS_SET_UV(e->w << 4, e->h << 4), GS_REG_UV);
            packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)((sx + hx) * 16), (int)((sy + hy) * 16), z), GS_REG_XYZ2);
            regs += 5;
        }
        tag->dw[0] = GIF_SET_TAG(regs, 1, 0, 0, GIF_FLG_PACKED, 1);
        packet2_vif_close_direct_auto(vif[cur]);
        packet2_chain_close_tag(vif[cur]);
        cur_tex = -1; /* ALPHA, TEST, ZBUF changed: the next texture packet puts them back */
    }
}

/* 2D quads (talk balloons and their text), queued and sent after the translucent models (render_end): HUD units,
 * no depth, alpha blended, textured with modulate or flat */
#define MAX_QUADS 1024
static Quad quads[MAX_QUADS];
static int nquads;

void render_quads(const Quad *q, int n)
{
    for (int i = 0; i < n && nquads < MAX_QUADS; i++)
        quads[nquads++] = q[i];
}

static void draw_quads(void)
{
    for (int i = 0; i < nquads;) {
        u32 t = quads[i].tex;
        int j = i;
        while (j < nquads && j - i < 64 && quads[j].tex == t)
            j++;
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 4 + (j - i) * 10 > PACKET_QW)
            flush_vif();
        set_tex(t);
        packet2_chain_open_cnt(vif[cur], 0, 0, 0);
        packet2_vif_flush(vif[cur], 0);
        packet2_vif_open_direct(vif[cur], 0);
        qword_t *tag = (qword_t *)vif[cur]->next;
        packet2_add_2x_s64(vif[cur], 0, GIF_REG_AD);
        packet2_add_2x_s64(vif[cur], GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
        packet2_add_2x_s64(vif[cur], GS_SET_TEST(1, ATEST_METHOD_GREATER, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST);
        packet2_add_2x_s64(vif[cur], GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 1), GS_REG_ZBUF);
        u32 regs = 3;
        for (; i < j; i++) {
            const Quad *q = &quads[i];
            float x0 = 2048.0f - SCREEN_W / 2 + q->x, y0 = 2048.0f - SCREEN_H / 2 + q->y * YS;
            float x1 = x0 + q->w, y1 = y0 + q->h * YS;
            int skew = q->vw != 0.0f || q->uh != 0.0f || q->pad;
            skew &= t != NO_TEX; /* skewed: ST (floats: its corners may fall off the texture, clamped there) */
            packet2_add_2x_s64(vif[cur], GS_SET_PRIM(skew ? PRIM_TRIANGLE_STRIP : PRIM_SPRITE, 0, t != NO_TEX, 0, 1, 0, !skew, 0, 0), GS_REG_PRIM);
            packet2_add_2x_s64(vif[cur], GS_SET_RGBAQ(q->rgba & 0xFF, q->rgba >> 8 & 0xFF, q->rgba >> 16 & 0xFF, q->rgba >> 24, 0x3F800000), GS_REG_RGBAQ);
            regs += 2;
            if (skew) { /* top left, top right, bottom left, bottom right */
                const TexEntry *e = tex_scene->tex[t].e;
                float kw = 1.0f / (float)(1 << e->tw), kh = 1.0f / (float)(1 << e->th);
                for (int c = 0; c < 4; c++) {
                    float fx = (float)(c & 1), fy = (float)(c >> 1);
                    union { float f; u32 u; } S = {(q->u + fx * q->uw + fy * q->uh) * kw}, T = {(q->v + fx * q->vw + fy * q->vh) * kh};
                    packet2_add_2x_s64(vif[cur], (u64)T.u << 32 | S.u, GS_REG_ST);
                    packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)((fx ? x1 : x0) * 16), (int)((fy ? y1 : y0) * 16), 0), GS_REG_XYZ2);
                }
                regs += 8;
                continue;
            }
            packet2_add_2x_s64(vif[cur], GS_SET_UV((int)(q->u * 16), (int)(q->v * 16)), GS_REG_UV);
            packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)(x0 * 16), (int)(y0 * 16), 0), GS_REG_XYZ2);
            packet2_add_2x_s64(vif[cur], GS_SET_UV((int)((q->u + q->uw) * 16), (int)((q->v + q->vh) * 16)), GS_REG_UV);
            packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)(x1 * 16), (int)(y1 * 16), 0), GS_REG_XYZ2);
            regs += 4;
        }
        tag->dw[0] = GIF_SET_TAG(regs, 1, 0, 0, GIF_FLG_PACKED, 1);
        packet2_vif_close_direct_auto(vif[cur]);
        packet2_chain_close_tag(vif[cur]);
        cur_tex = -1;
    }
    nquads = 0;
}

/* WaterPlant strips: a triangle strip of 8 points each, alpha blended (pad bit 0: added, Cs x As + Cd), textured (UV), no depth write; a strip with a
 * point behind the camera or far off screen is left out */
void render_strips(const Strip *st, int n)
{
    for (int i = 0; i < n; i++) {
        const Strip *s = &st[i];
        float sx[8], sy[8];
        u32 sz[8];
        int ok = 1;
        for (int v = 0; v < 8 && ok; v++) {
            const float *p = s->p[3 - v / 2][v & 1]; /* top first: v 0 at the top */
            float xv = view[0] * p[0] + view[4] * p[1] + view[8] * p[2] + view[12];
            float yv = view[1] * p[0] + view[5] * p[1] + view[9] * p[2] + view[13];
            float zv = view[2] * p[0] + view[6] * p[1] + view[10] * p[2] + view[14];
            if (zv < NEAR || zv > FAR) { ok = 0; break; }
            float k = 2048.0f / zv;
            sx[v] = 2048.0f + shake_x + proj[0] * xv * k;
            sy[v] = 2048.0f + shake_y + proj[5] * yv * k;
            if (sx[v] < 2048 - SCREEN_W || sx[v] > 2048 + SCREEN_W || sy[v] < 2048 - SCREEN_H || sy[v] > 2048 + SCREEN_H) ok = 0;
            sz[v] = (u32)(((proj[10] * zv + proj[14]) / zv + 1.0f) * (float)(Z_MAX / 2));
        }
        if (!ok)
            continue;
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 4 + 17 > PACKET_QW)
            flush_vif();
        set_tex(s->tex);
        const TexEntry *e = tex_scene->tex[s->tex].e;
        packet2_chain_open_cnt(vif[cur], 0, 0, 0);
        packet2_vif_flush(vif[cur], 0);
        packet2_vif_open_direct(vif[cur], 0);
        qword_t *tag = (qword_t *)vif[cur]->next;
        packet2_add_2x_s64(vif[cur], 0, GIF_REG_AD);
        packet2_add_2x_s64(vif[cur], s->pad & 1 ? GS_SET_ALPHA(0, 2, 0, 1, 0) : GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
        packet2_add_2x_s64(vif[cur], GS_SET_TEST(1, ATEST_METHOD_GREATER, 0x10, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL), GS_REG_TEST);
        packet2_add_2x_s64(vif[cur], GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 1), GS_REG_ZBUF);
        packet2_add_2x_s64(vif[cur], GS_SET_PRIM(PRIM_TRIANGLE_STRIP, 0, 1, 0, 1, 0, 1, 0, 0), GS_REG_PRIM);
        packet2_add_2x_s64(vif[cur], GS_SET_RGBAQ(s->rgba & 0xFF, s->rgba >> 8 & 0xFF, s->rgba >> 16 & 0xFF, s->rgba >> 24, 0x3F800000), GS_REG_RGBAQ);
        for (int v = 0; v < 8; v++) {
            packet2_add_2x_s64(vif[cur], GS_SET_UV((v & 1) ? e->w << 4 : 0, (e->h * (v / 2) / 3) << 4), GS_REG_UV);
            packet2_add_2x_s64(vif[cur], GS_SET_XYZ((int)(sx[v] * 16), (int)(sy[v] * 16), sz[v]), GS_REG_XYZ2);
        }
        tag->dw[0] = GIF_SET_TAG(5 + 16, 1, 0, 0, GIF_FLG_PACKED, 1);
        packet2_vif_close_direct_auto(vif[cur]);
        packet2_chain_close_tag(vif[cur]);
        cur_tex = -1; /* ALPHA, TEST, ZBUF changed */
    }
}

/* BloomEffect after Petari, on the GS without a capture of the frame (no VRAM for one): the frame's alpha cleared,
 * the bloom models drawn into it only (FBMSK the colour; the scene's depth hides them), then the frame times that
 * alpha box-filtered to a quarter (preDraw's capture and the 4 x downsample), blurred there (sL1RadAndOfs: radii 0.01
 * and 0.02, 6 taps each at intensity1 / 255) and from that at an eighth (sL2RadAndOfs: 0.04, 0.07, 0.09, 12 taps at
 * intensity2 / 255), both added to the frame at intensity / 255. ponytail: the bloom takes the scene's colour under
 * its models (not the models' own), no threshold mask (sMaskThreshold), aspect 4 / 3 */
#define MAX_BLOOM 32
static struct { const Model *md; float m[16]; } bloom_list[MAX_BLOOM];
static int nbloom;
static u8 bloom_k, bloom_k1, bloom_k2;

void render_bloom_model(const Model *md, const float world[16])
{
    if (md && nbloom < MAX_BLOOM) { bloom_list[nbloom].md = md; memcpy(bloom_list[nbloom].m, world, 64); nbloom++; }
}

void render_bloom(u8 intensity, u8 intensity1, u8 intensity2) { bloom_k = intensity; bloom_k1 = intensity1; bloom_k2 = intensity2; }

/* a DIRECT A+D list in the VIF chain for at most n registers; ad_close sets its GIF tag's count */
static qword_t *ad_tag;
static void ad_open(int n)
{
    if (packet2_get_qw_count(vif[cur]) + n + 8 > PACKET_QW)
        flush_vif();
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flush(vif[cur], 0);
    packet2_vif_open_direct(vif[cur], 0);
    ad_tag = (qword_t *)vif[cur]->next;
    packet2_add_2x_s64(vif[cur], 0, GIF_REG_AD);
}
#define AD(v, r) packet2_add_2x_s64(vif[cur], (v), (r))
static void ad_close(void)
{
    ad_tag->dw[0] = GIF_SET_TAG((qword_t *)vif[cur]->next - ad_tag - 1, 1, 0, 0, GIF_FLG_PACKED, 1);
    packet2_vif_close_direct_auto(vif[cur]);
    packet2_chain_close_tag(vif[cur]);
}

/* a textured sprite from (x0, y0)-(x1, y1) of the bound buffer with texels (u0, v0)-(u1, v1), colour c (A 0x80) */
static void ad_sprite(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, int c)
{
    AD(GS_SET_PRIM(PRIM_SPRITE, 0, 1, 0, 1, 0, 1, 0, 0), GS_REG_PRIM);
    AD(GS_SET_RGBAQ(c, c, c, 0x80, 0x3F800000), GS_REG_RGBAQ);
    AD(GS_SET_UV((int)(u0 * 16), (int)(v0 * 16)), GS_REG_UV);
    AD(GS_SET_XYZ((int)((2048 - SCREEN_W / 2 + x0) * 16), (int)((2048 - SCREEN_H / 2 + y0) * 16), 0), GS_REG_XYZ2);
    AD(GS_SET_UV((int)(u1 * 16), (int)(v1 * 16)), GS_REG_UV);
    AD(GS_SET_XYZ((int)((2048 - SCREEN_W / 2 + x1) * 16), (int)((2048 - SCREEN_H / 2 + y1) * 16), 0), GS_REG_XYZ2);
}
/* a buffer to draw into, an untextured clear of (w, h) in black */
static void ad_target(u32 addr, int fbw, int w, int h)
{
    AD(GS_SET_FRAME(addr >> 11, fbw, GS_PSM_32, 0), GS_REG_FRAME);
    AD(GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 0, 0, 0), GS_REG_PRIM);
    AD(GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
    AD(GS_SET_XYZ((2048 - SCREEN_W / 2) << 4, (2048 - SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    AD(GS_SET_XYZ((2048 - SCREEN_W / 2 + w) << 4, (2048 - SCREEN_H / 2 + h) << 4, 0), GS_REG_XYZ2);
}
/* a buffer as the texture (bilinear, clamped to w x h, tcc: its alpha too) */
static void ad_source_psm(u32 addr, int tbw, int psm, int tw, int th, int w, int h, int tcc)
{
    AD(0, GS_REG_TEXFLUSH);
    AD(GS_SET_TEX0(addr >> 6, tbw, psm, tw, th, tcc, 0, 0, 0, 0, 0, 0), GS_REG_TEX0);
    AD(GS_SET_TEX1(1, 0, 1, 1, 0, 0, 0), GS_REG_TEX1);
    AD(GS_SET_CLAMP(2, 2, 0, w - 1, 0, h - 1), GS_REG_CLAMP);
}
#define ad_source(addr, tbw, tw, th, w, h, tcc) ad_source_psm(addr, tbw, GS_PSM_32, tw, th, w, h, tcc)

static void bloom_pass(void)
{
    if (!bloom_k || !nbloom) { nbloom = 0; return; }
    u32 fb = frame[back].address, fw = SCREEN_W >> 6;
    /* the frame's alpha to 0, then the bloom models into it alone */
    ad_open(8);
    AD(GS_SET_FBA(0), GS_REG_FBA);
    AD(GS_SET_DTHE(0), GS_REG_DTHE);
    AD(GS_SET_FRAME(fb >> 11, fw, FB_PSM, 0x00FFFFFF), GS_REG_FRAME); /* colour kept (FBMSK in RGBA32 bits for any PSM) */
    AD(GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST);
    AD(GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 0, 0, 0), GS_REG_PRIM);
    AD(GS_SET_RGBAQ(0, 0, 0, 0, 0x3F800000), GS_REG_RGBAQ);
    AD(GS_SET_XYZ((2048 - SCREEN_W / 2) << 4, (2048 - SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    AD(GS_SET_XYZ((2048 + SCREEN_W / 2) << 4, (2048 + SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    ad_close();
    cur_tex = -1;
    bloom_drawing = 1;
    for (int i = 0; i < nbloom; i++)
        draw_model(bloom_list[i].md, bloom_list[i].m, NULL);
    bloom_drawing = 0;
    nbloom = 0;
    /* the frame x its alpha to a quarter: 4 bilinear taps 1 texel off each way (a 4 x 4 box) */
    const float aspect = 4.0f / 3.0f;
    ad_open(400);
    ad_target(bloom_q, 3, 160, 56);
    AD(GS_SET_ALPHA(0, 2, 0, 1, 0), GS_REG_ALPHA);           /* Cs As + Cd */
    AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 1), GS_REG_ZBUF);
    AD(GS_SET_TEXA(0, 0, 0x80), GS_REG_TEXA); /* 16-bit frame: its alpha bit reads 0 or 0x80 */
    ad_source_psm(fb, fw, FB_PSM, 10, FB_TH, SCREEN_W, SCREEN_H, 1);
    for (int k = 0; k < 4; k++) {
        float du = (k & 1) ? 1.0f : -1.0f, dv = (k & 2) ? 1.0f : -1.0f;
        ad_sprite(0, 0, 160, 56, du, dv, SCREEN_W + du, SCREEN_H + dv, 0x20);
    }
    /* L1 at the quarter: blurTexture(sL1RadAndOfs), each tap at intensity1 */
    ad_target(bloom_q2, 3, 160, 56);
    AD(GS_SET_ALPHA(0, 2, 2, 1, 0x80), GS_REG_ALPHA);        /* Cs + Cd */
    ad_source(bloom_q, 3, 8, 6, 160, 56, 0);
    static const float r1[2][2] = {{0.01f, 0.0f}, {0.02f, 0.52f}}, r2[3] = {0.04f, 0.07f, 0.09f};
    int c1 = bloom_k1 * 0x80 / 255, c2 = bloom_k2 * 0x80 / 255;
    for (int r = 0; r < 2; r++)
        for (int i = 0; i < 6; i++) {
            float a = r1[r][1] + i * 6.2831853f / 6, du = -r1[r][0] / aspect * cosf(a) * 160, dv = -r1[r][0] * sinf(a) * 56;
            ad_sprite(0, 0, 160, 56, du, dv, 160 + du, 56 + dv, c1);
        }
    /* L2 at an eighth from L1: sL2RadAndOfs, 12 taps each at intensity2 */
    ad_target(bloom_e, 2, 80, 28);
    ad_source(bloom_q2, 3, 8, 6, 160, 56, 0);
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 12; i++) {
            float a = i * 6.2831853f / 12, du = -r2[r] / aspect * cosf(a) * 160, dv = -r2[r] * sinf(a) * 56;
            ad_sprite(0, 0, 80, 28, du, dv, 160 + du, 56 + dv, c2);
        }
    /* both added to the frame at intensity */
    AD(GS_SET_FRAME(fb >> 11, fw, FB_PSM, 0), GS_REG_FRAME);
    int c = bloom_k * 0x80 / 255;
    ad_source(bloom_q2, 3, 8, 6, 160, 56, 0);
    ad_sprite(0, 0, SCREEN_W, SCREEN_H, 0, 0, 160, 56, c);
    ad_source(bloom_e, 2, 7, 5, 80, 28, 0);
    ad_sprite(0, 0, SCREEN_W, SCREEN_H, 0, 0, 80, 28, c);
    AD(test_z, GS_REG_TEST);
    AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0), GS_REG_ZBUF);
    AD(GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
    AD(GS_SET_DTHE(FB_PSM != GS_PSM_32), GS_REG_DTHE); /* env_dither's */
    AD(0, GS_REG_TEXFLUSH);
    ad_close();
    cur_tex = -1;
}

#define MESH_MAX 2048
static int mesh_same;
void render_mesh_same(void) { mesh_same = 1; }

void render_mesh(const float (*pos)[3], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, int blend)
{
    render_mesh_c(pos, uv, nvert, idx, ntri, tex, blend, 0x80808080u);
}

void render_mesh_c(const float (*pos)[3], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, int blend, u32 rgba)
{
    static float msx[MESH_MAX], msy[MESH_MAX];
    static u32 msz[MESH_MAX];
    static u8 mok[MESH_MAX];
    const TexEntry *e = tex == NO_TEX ? NULL : tex_scene->tex[tex].e; /* NO_TEX: flat, the colour alone */
    if (nvert > MESH_MAX) nvert = MESH_MAX;
    for (int v = mesh_same ? nvert : 0; v < nvert; v++) { /* each vertex once: screen x, y, depth; mok 0 behind the camera or far off */
        const float *p = pos[v];
        float xv = view[0] * p[0] + view[4] * p[1] + view[8] * p[2] + view[12];
        float yv = view[1] * p[0] + view[5] * p[1] + view[9] * p[2] + view[13];
        float zv = view[2] * p[0] + view[6] * p[1] + view[10] * p[2] + view[14];
        mok[v] = zv >= NEAR && zv <= FAR;
        if (!mok[v]) continue;
        float k = 2048.0f / zv;
        msx[v] = 2048.0f + shake_x + proj[0] * xv * k;
        msy[v] = 2048.0f + shake_y + proj[5] * yv * k;
        mok[v] = msx[v] >= 0 && msx[v] <= 4095 && msy[v] >= 0 && msy[v] <= 4095;
        msz[v] = (u32)(((proj[10] * zv + proj[14]) / zv + 1.0f) * (float)(Z_MAX / 2));
    }
    mesh_same = 0;
    for (int t0 = 0; t0 < ntri; t0 += 40) { /* 40 triangles a GIF packet */
        int n = ntri - t0 < 40 ? ntri - t0 : 40;
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 6 * 3 * n + 8 > PACKET_QW)
            flush_vif();
        set_tex(tex);
        ad_open(0);
        AD(blend == 2 ? GS_SET_ALPHA(0, 2, 0, 1, 0) : GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA); /* 2: Cs x As + Cd */
        AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, blend ? 1 : 0), GS_REG_ZBUF);
        AD(GS_SET_PRIM(PRIM_TRIANGLE, 0, e != NULL, 0, blend ? 1 : 0, 0, 1, 0, 0), GS_REG_PRIM);
        AD(GS_SET_RGBAQ(rgba & 0xFF, rgba >> 8 & 0xFF, rgba >> 16 & 0xFF, rgba >> 24, 0x3F800000), GS_REG_RGBAQ);
        for (int t = t0; t < t0 + n; t++) {
            const u16 *ix = idx + 3 * t;
            if (ix[0] >= nvert || ix[1] >= nvert || ix[2] >= nvert || !mok[ix[0]] || !mok[ix[1]] || !mok[ix[2]]) continue;
            for (int v = 0; v < 3; v++) {
                const float *q = e ? uv[ix[v]] : NULL;
                if (e) AD(GS_SET_UV((int)(q[0] * e->w * 16) & 0x3FFF, (int)(q[1] * e->h * 16) & 0x3FFF), GS_REG_UV);
                AD(GS_SET_XYZ((int)(msx[ix[v]] * 16), (int)(msy[ix[v]] * 16), msz[ix[v]]), GS_REG_XYZ2);
            }
        }
        AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0), GS_REG_ZBUF);
        ad_close();
        cur_tex = -1; /* ALPHA, ZBUF changed */
    }
}

void render_mesh2d(const float (*xy)[2], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, u32 rgba, int add)
{
    const TexEntry *e = tex == NO_TEX ? NULL : tex_scene->tex[tex].e; /* NO_TEX: flat, the colour alone */
    for (int t0 = 0; t0 < ntri; t0 += 40) { /* 40 triangles a GIF packet */
        int n = ntri - t0 < 40 ? ntri - t0 : 40;
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 6 * 3 * n + 8 > PACKET_QW)
            flush_vif();
        set_tex(tex);
        ad_open(0);
        AD(add ? GS_SET_ALPHA(0, 2, 0, 1, 0) : GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA); /* Cs x As + Cd, else a blend */
        AD(GS_SET_TEST(1, ATEST_METHOD_GREATER, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST);
        AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 1), GS_REG_ZBUF);
        AD(GS_SET_PRIM(PRIM_TRIANGLE, 0, e != NULL, 0, 1, 0, 1, 0, 0), GS_REG_PRIM);
        AD(GS_SET_RGBAQ(rgba & 0xFF, rgba >> 8 & 0xFF, rgba >> 16 & 0xFF, rgba >> 24, 0x3F800000), GS_REG_RGBAQ);
        for (int t = t0; t < t0 + n; t++) {
            const u16 *ix = idx + 3 * t;
            if (ix[0] >= nvert || ix[1] >= nvert || ix[2] >= nvert) continue;
            for (int v = 0; v < 3; v++) {
                const float *q = uv[ix[v]], *p = xy[ix[v]];
                float x = 2048.0f - SCREEN_W / 2 + p[0], y = 2048.0f - SCREEN_H / 2 + p[1] * YS;
                x = fminf(fmaxf(x, 0.0f), 4095.0f); y = fminf(fmaxf(y, 0.0f), 4095.0f);
                if (e) AD(GS_SET_UV((int)(q[0] * e->w * 16) & 0x3FFF, (int)(q[1] * e->h * 16) & 0x3FFF), GS_REG_UV);
                AD(GS_SET_XYZ((int)(x * 16), (int)(y * 16), Z_MAX), GS_REG_XYZ2);
            }
        }
        AD(GS_SET_TEST(1, ATEST_METHOD_GREATER, 0, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL), GS_REG_TEST);
        AD(GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0), GS_REG_ZBUF);
        ad_close();
        cur_tex = -1; /* ALPHA, TEST changed */
    }
}

/* the frame's world finished now: its blended groups and the bloom (render_end's), then the 2D queued so far; what is
 * drawn after (a pause menu's layouts) covers them */
void render_world_done(void)
{
    for (int i = 0; i < nxlu; i++) {
        model_textures(xlu[i].md);
        if (xlu[i].md->lit)
            send_light(xlu[i].light);
        call_chain(xlu[i].md->chain_xlu, xlu[i].m);
    }
    nxlu = 0;
    bloom_pass();
    draw_quads();
}

void render_quads_flush(void) { draw_quads(); }

u32 render_end(void)
{
    u32 c0 = cycles();
    render_world_done();
    /* the HUD (gif packet) and a FINISH close the chain: FLUSH (VU1 and its XGKICKs done), then DIRECT from a REF */
    if (!text_open)
        packet2_reset(gif, 0);
    packet2_update(gif, draw_enable_tests(gif->next, 0, &zbuf));
    packet2_update(gif, draw_finish(gif->next));
    text_open = 0;
    u32 qw = packet2_get_qw_count(gif);
    SyncDCache(gif->base, (u8 *)gif->base + qw * 16);
    if (packet2_get_qw_count(vif[cur]) + 8 > PACKET_QW)
        flush_vif();
    packet2_chain_ref(vif[cur], gif->base, qw, 0, 0, 0);
    packet2_vif_flush(vif[cur], 0);
    packet2_vif_open_direct(vif[cur], 0);
    packet2_vif_close_direct_manual(vif[cur], qw);
    *(volatile u64 *)0x12001000 = 2; /* no stale FINISH: the flip waits for this chain's */
    flip_addr = frame[back].address;
    flip_pending = 1;
    flush_vif(); /* the frame's last part goes; not waited for */
#ifdef SYNC_FLIP /* the old behaviour, for comparisons: the frame on screen before the next one's logic */
    render_flip_wait();
#endif
    back ^= 1;
    return cycles() - c0;
}

/* movie.c: the texture cache's VRAM (its first block), free while no scene is loaded */
static u32 movie_blk; /* render_movie_mode: the cache's first blocks under the 32-bit frames */
u32 render_free_block(void) { return block_first + movie_blk; }
void render_reserve_blocks(u32 n) { block_rsv = n + movie_blk; } /* tex_alloc starts past them */

/* movie.c, with no scene loaded: the movie on 32-bit frames (the 16-bit ones band and grain its gradients). Two
 * 640 x 448 PSMCT32 buffers from the first frame's address cover both 16-bit frames, the Z buffer, the bloom buffers
 * and the cache's first blocks; the cache is emptied and starts past them. Off: the 16-bit frames back, black. */
void render_movie_mode(int on)
{
    if (FB_PSM == GS_PSM_32) return; /* LORES: the frames are 32-bit already */
    render_flip_wait();
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    static u32 addr16[2];
    if (on) {
        u32 size = SCREEN_W * SCREEN_H; /* words */
        addr16[0] = frame[0].address; addr16[1] = frame[1].address;
        frame[1].address = frame[0].address + size;
        frame[0].psm = frame[1].psm = fb_psm = GS_PSM_32;
        u32 end = (frame[0].address + 2 * size + 63) / 64;
        movie_blk = end > (u32)block_first ? end - block_first : 0;
        for (int b = block_first; b < block_end; b++) block_tex[b] = -1; /* every texture out */
        for (int t = 0; t < SCENE_MAX_TEX; t++) tex_block[t] = -1;
        block_rsv = movie_blk;
    } else {
        frame[0].address = addr16[0]; frame[1].address = addr16[1];
        frame[0].psm = frame[1].psm = fb_psm = FB_PSM;
        movie_blk = block_rsv = 0;
    }
    zbuffer_t mz = zbuf;
    mz.mask = 1; /* the Z buffer lies under the second 32-bit frame */
    packet2_reset(gif, 0);
    for (int i = 0; i < 2; i++) { /* both black: neither shows the other format's pixels */
        packet2_update(gif, draw_setup_environment(gif->next, 0, &frame[i], &mz));
        packet2_update(gif, draw_dithering(gif->next, 0));
        packet2_update(gif, draw_primitive_xyoffset(gif->next, 0, 2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2));
        packet2_update(gif, draw_disable_tests(gif->next, 0, &mz));
        packet2_update(gif, draw_clear(gif->next, 0, 2048.0f - SCREEN_W / 2, 2048.0f - SCREEN_H / 2, SCREEN_W, SCREEN_H, 0, 0, 0));
    }
    packet2_update(gif, draw_finish(gif->next));
    dma_channel_send_packet2(gif, DMA_CHANNEL_GIF, 1);
    draw_wait_finish();
    back = 1;
    graph_set_framebuffer(0, frame[0].address, SCREEN_W, fb_psm, 0, 0);
}

/* a decoded movie picture (w x h, its upload chain already built for render_free_block) on black, scaled to the
 * frame's width and letterboxed; covered by `fade` (0xAABBGGRR, A 0x80 opaque) */
void render_picture(const void *upload, int w, int h, u32 fade, void (*overlay)(void))
{
    render_flip_wait();
    int dh = (int)(h * SCREEN_W / w * YS), y0 = (SCREEN_H - dh) / 2; /* in frame lines (YS: 224 or 448 of them) */
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_chain(DMA_CHANNEL_GIF, (void *)upload, 0, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    zbuffer_t mz = zbuf;
    mz.mask = fb_psm != FB_PSM; /* 32-bit frames: the Z buffer is under the second one */
    packet2_reset(gif, 0);
    packet2_update(gif, draw_setup_environment(gif->next, 0, &frame[back], &mz));
    packet2_update(gif, fb_psm == GS_PSM_32 ? draw_dithering(gif->next, 0) : env_dither(gif->next));
    packet2_update(gif, draw_primitive_xyoffset(gif->next, 0, 2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2));
    packet2_update(gif, draw_disable_tests(gif->next, 0, &mz));
    packet2_update(gif, draw_clear(gif->next, 0, 2048.0f - SCREEN_W / 2, 2048.0f - SCREEN_H / 2, SCREEN_W, SCREEN_H, 0, 0, 0));
    packet2_update(gif, draw_finish(gif->next));
    dma_channel_send_packet2(gif, DMA_CHANNEL_GIF, 1);
    draw_wait_finish();
    packet2_reset(vif[cur], 0);
    ad_open(20);
    AD(GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
    ad_source(render_free_block() << 6, SCREEN_W >> 6, 10, 9, w, h, 0);
    ad_sprite(0, y0, SCREEN_W, y0 + dh, 0, 0, w, h, 0x80);
    if (overlay) { /* MovieSubtitles (DrawType_MovieSubtitles), under the wipe */
        ad_close();
        overlay();
        ad_open(8);
        AD(GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
    }
    if (fade >> 24) {
        AD(GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 1, 0, 0, 0, 0), GS_REG_PRIM);
        AD(GS_SET_RGBAQ(fade & 0xFF, (fade >> 8) & 0xFF, (fade >> 16) & 0xFF, fade >> 24, 0x3F800000), GS_REG_RGBAQ);
        AD(GS_SET_XYZ((2048 - SCREEN_W / 2) << 4, (2048 - SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
        AD(GS_SET_XYZ((2048 + SCREEN_W / 2) << 4, (2048 + SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    }
    ad_close();
    flush_vif();
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    cur_tex = -1;
}

/* the frame drawn shown now (movie.c, at a vblank of its own pacing) */
void render_show(void)
{
    render_flip_wait();
    graph_set_framebuffer(0, frame[back].address, SCREEN_W, fb_psm, 0, 0);
    back ^= 1;
}

u32 render_tris_drawn(void) { return tris_drawn; }

int render_visible(const float p[3], float rad) /* draw_model's test: a sphere in front of the eye, in the frustum */
{
    float x = view[0] * p[0] + view[4] * p[1] + view[8] * p[2] + view[12];
    float y = view[1] * p[0] + view[5] * p[1] + view[9] * p[2] + view[13];
    float z = view[2] * p[0] + view[6] * p[1] + view[10] * p[2] + view[14];
    if (z + rad < NEAR || z - rad > FAR) return 0;
    return !(frustum[0] * fabsf(x) - frustum[1] * z > rad || frustum[2] * fabsf(y) - frustum[3] * z > rad);
}

int render_clip_judge(const float p[3], float rad) /* ClippingJudge: this view's frustum, near 500, far 10000 */
{
    float x = view[0] * p[0] + view[4] * p[1] + view[8] * p[2] + view[12];
    float y = view[1] * p[0] + view[5] * p[1] + view[9] * p[2] + view[13];
    float z = view[2] * p[0] + view[6] * p[1] + view[10] * p[2] + view[14];
    if (z + rad < 500.0f || z - rad > 10000.0f) return 1;
    return frustum[0] * fabsf(x) - frustum[1] * z > rad || frustum[2] * fabsf(y) - frustum[3] * z > rad;
}

void render_tex_stats(u32 *misses, u32 *overflow, u32 *syncs)
{
    *misses = tex_misses; *overflow = tex_overflow; *syncs = tex_syncs;
}
