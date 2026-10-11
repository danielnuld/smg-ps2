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

/* VU1 renderer: the EE only builds DMA chains that point at the blob's VU-ready vertex data (VUMD chunk) and at one
 * matrix per model instance; VU1 (vu1_tex.vsm) transforms, rejects off-band triangles, projects, computes STQ and
 * XGKICKs to the GS. Set-up follows ps2sdk samples/draw/vu1 (libpacket2, libdraw, libgraph).
 * Textures (TEXT chunk, PSMT4 + 16-entry CLUT) are uploaded to VRAM once at load: Gateway's set fits the VRAM left
 * after two frames and Z (ponytail: per-frame upload over PATH3 when a scene's set does not fit, milestone 5).
 * A texture change is a FLUSH + DIRECT A+D packet (TEX0 with CLUT load, TEX1, CLAMP, TEST) in the VIF chain. */

extern u32 VU1Tex_CodeStart __attribute__((section(".vudata")));
extern u32 VU1Tex_CodeEnd __attribute__((section(".vudata")));

#define SCREEN_W 640
/* 640 x 224 buffers shown in FRAME mode (each field reads every line): the picture is the same on both fields, so it
 * does not bob a line per frame under PCSX2's deinterlacing (a 448-line buffer read every other line did: each
 * buffer always lands on the same field). Layout and projection stay in 448-line units (DISP_H), scaled by YS. */
#define SCREEN_H 224
#define DISP_H 448
#define YS ((float)SCREEN_H / DISP_H)
#define BATCH 72           /* vertices per VU1 batch: 6n + 4m + 2 qwords (m <= 10 matrices) must fit a 496-qword
                            * buffer, and n a multiple of 12 so the 4-byte colour and UV arrays of the next batch
                            * start on a qword */
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
static float frustum[4];        /* side planes in view space: x n, z n for |x| and |y| (render_set_fovy) */
static float (*mats)[16];        /* one matrix per instance this frame, read by DMA */
static int nmats;
#define MAX_MAT 10               /* matrices per batch (a J3D matrix group holds up to 10) */
#define HDR(t, m, n) (headers + 2 * (((t) * MAX_MAT + (m) - 1) * (BATCH / 3 + 1) + (n) / 3))
static qword_t *headers;         /* [mode][matrices 1..MAX_MAT][count 3..BATCH]: {n, 4m} ints + GIF tag; mode
                                  * bit 0 textured, bit 1 alpha blended (group_mode) */
#define MODES 4
static qword_t scale_qw __attribute__((aligned(16)));
#define HDRS(t, n) (headers_static + 2 * ((t) * (BATCH / 3 + 1) + (n) / 3))
static qword_t *headers_static;  /* [mode][count]: {n, 0, matrix base 1} + GIF tag, for prebuilt chains */
static u32 tris_drawn;
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

void render_init(void)
{
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_VIF1, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    dma_channel_fast_waits(DMA_CHANNEL_VIF1);

    /* microprogram + double buffer (base 8, 496 qwords each), as in the ps2sdk sample */
    u32 sz = packet2_utils_get_packet_size_for_program(&VU1Tex_CodeStart, &VU1Tex_CodeEnd) + 1;
    packet2_t *p = packet2_create(sz, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_vif_add_micro_program(p, 0, &VU1Tex_CodeStart, &VU1Tex_CodeEnd);
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);
    p = packet2_create(2, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_utils_vu_add_double_buffer(p, 8, 496);
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);

    for (int i = 0; i < 2; i++) {
        frame[i].width = SCREEN_W; frame[i].height = SCREEN_H; frame[i].mask = 0; frame[i].psm = GS_PSM_32;
        frame[i].address = graph_vram_allocate(SCREEN_W, SCREEN_H, GS_PSM_32, GRAPH_ALIGN_PAGE);
    }
    zbuf.enable = DRAW_ENABLE; zbuf.mask = 0; zbuf.method = ZTEST_METHOD_GREATER_EQUAL; zbuf.zsm = GS_ZBUF_24;
    zbuf.address = graph_vram_allocate(SCREEN_W, SCREEN_H, GS_ZBUF_24, GRAPH_ALIGN_PAGE);
    graph_initialize(frame[0].address, SCREEN_W, SCREEN_H, GS_PSM_32, 0, 0);
    graph_set_mode(GRAPH_MODE_INTERLACED, graph_get_region(), GRAPH_MODE_FRAME, GRAPH_DISABLE);
    graph_set_screen(0, 16, SCREEN_W, DISP_H);
    graph_set_framebuffer(0, frame[0].address, SCREEN_W, GS_PSM_32, 0, 0);
    graph_set_output(GRAPH_ENABLE, GRAPH_DISABLE, 1, 0, 0, 0xFF); /* circuit 1, alpha from ALP (frame alpha is 0x80) */
    back = 1;
    p = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    packet2_update(p, draw_setup_environment(p->next, 0, &frame[back], &zbuf));
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
    mats = mem_alloc(MEM_RENDER, MAX_INST * 64);
    headers = mem_alloc(MEM_RENDER, MODES * MAX_MAT * (BATCH / 3 + 1) * 2 * 16);
    for (int t = 0; t < MODES; t++)
        for (int m = 1; m <= MAX_MAT; m++)
            for (int n = 3; n <= BATCH; n += 3) {
                qword_t *h = HDR(t, m, n);
                h->sw[0] = n;
                h->sw[1] = 4 * m;
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
    /* scale constant at VU mem 0, below the double buffers: written once */
    ((float *)&scale_qw)[0] = 2048.0f;
    ((float *)&scale_qw)[1] = 2048.0f;
    ((float *)&scale_qw)[2] = (float)0xFFFFFF / 32.0f;
    p = packet2_create(4, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    packet2_utils_vu_add_unpack_data(p, 0, &scale_qw, 1, 0);
    packet2_utils_vu_add_end_tag(p);
    send_vif_wait(p);
    packet2_free(p);
    test_z = GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_GREATER_EQUAL);
    init_blend_states();

    render_set_fovy(45.0f);
}

/* projection (column-major, v' = P v): x, y to the GS's +-2048 guard band, depth near -> 1, far -> -1 */
void render_set_fovy(float deg)
{
    float f = (DISP_H / 2) / tanf(deg * 3.14159265f / 360.0f);
    float B = 2.0f / (1.0f / NEAR - 1.0f / FAR), A = 1.0f - B / NEAR;
    memset(proj, 0, sizeof proj);
    proj[0] = f / 2048.0f;
    proj[5] = -f * YS / 2048.0f;
    proj[10] = A; proj[11] = 1.0f;
    proj[14] = B;
    float lx = sqrtf(f * f + (SCREEN_W / 2) * (SCREEN_W / 2)), ly = sqrtf(f * f + (DISP_H / 2) * (DISP_H / 2));
    frustum[0] = f / lx; frustum[1] = (SCREEN_W / 2) / lx;
    frustum[2] = f / ly; frustum[3] = (DISP_H / 2) / ly;
}

void render_begin(const float eye[3], const float target[3], const float up[3])
{
    float fw[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    float fl = sqrtf(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
    fw[0] /= fl; fw[1] /= fl; fw[2] /= fl;
    float s[3] = {fw[1] * up[2] - fw[2] * up[1], fw[2] * up[0] - fw[0] * up[2], fw[0] * up[1] - fw[1] * up[0]};
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = {s[1] * fw[2] - s[2] * fw[1], s[2] * fw[0] - s[0] * fw[2], s[0] * fw[1] - s[1] * fw[0]};
    mat_identity(view);
    view[0] = s[0]; view[4] = s[1]; view[8] = s[2];
    view[1] = u[0]; view[5] = u[1]; view[9] = u[2];
    view[2] = fw[0]; view[6] = fw[1]; view[10] = fw[2];
    view[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    view[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    view[14] = -(fw[0] * eye[0] + fw[1] * eye[1] + fw[2] * eye[2]);

    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    packet2_reset(gif, 0);
    packet2_update(gif, draw_setup_environment(gif->next, 0, &frame[back], &zbuf));
    packet2_update(gif, draw_primitive_xyoffset(gif->next, 0, 2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2));
    packet2_update(gif, draw_disable_tests(gif->next, 0, &zbuf));
    packet2_update(gif, draw_clear(gif->next, 0, 2048.0f - SCREEN_W / 2, 2048.0f - SCREEN_H / 2, SCREEN_W, SCREEN_H, 0x08, 0x0C, 0x28));
    packet2_update(gif, draw_enable_tests(gif->next, 0, &zbuf));
    packet2_update(gif, draw_finish(gif->next));
    dma_channel_send_packet2(gif, DMA_CHANNEL_GIF, 1);
    draw_wait_finish();
    packet2_reset(vif[cur], 0);
    nmats = 0;
    tris_drawn = 0;
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

static int tex_alloc(int n, int align)
{
    for (int b = block_first, run = 0; b < block_end; b++) {
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
    while ((b = tex_alloc(n, T8(e) ? 4 : 1)) < 0) { /* evict the least recently used texture whose chain has completed */
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

void render_load_textures(const Scene *s, u32 first_tex, u32 first_model)
{
    u32 ntex = s->ntex;
    tex_scene = s;
    if (!tex_packets) { /* fixed slots: prebuilt chains point into them; the NO_TEX packet sits after the last */
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
        const TexEntry *t = s->tex[i].e;
        tex_block[i] = -1;
        tex_nblk[i] = blocks_of(t);
        tex_last[i] = 0;
        need += tex_nblk[i];
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

/* one indexed batch from `base` on (after the matrices): positions, UVs, colours; advances *p past its data */
static void add_batch(packet2_t *pk, const u8 **p, u32 ne, u32 nu, int textured, u32 base)
{
    const u8 *d = *p;
    unpack_narrow_to(pk, d, 8 * nu, base, nu, P2_UNPACK_V4_16, 0);
    d += (8 * nu + 15) & ~15u;
    if (textured) {
        unpack_narrow_to(pk, d, 4 * nu, base + nu, nu, P2_UNPACK_V2_16, 0);
        d += (4 * nu + 15) & ~15u;
    }
    unpack_narrow_to(pk, d, 4 * ne, base + 2 * nu, ne, P2_UNPACK_V4_8, 1);
    d += (4 * ne + 15) & ~15u;
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
#define BLEND_STATES (2 * 2 * 2 * 16)
static qword_t *blend_states;

static const qword_t *blend_state(const VuGroup *vg)
{
    int as = vg->tex != NO_TEX && tex_scene->tex[vg->tex].e->alpha;
    return blend_states + 4 * ((((vg->blend == 2) * 2 + !!vg->zwrite) * 2 + as) * 16 + (vg->alpha >> 4));
}

static void init_blend_states(void)
{
    blend_states = mem_alloc(MEM_RENDER, BLEND_STATES * 4 * 16);
    for (int i = 0; i < BLEND_STATES; i++) {
        int add = i >> 6 & 1, zw = i >> 5 & 1, as = i >> 4 & 1, a = (i & 15) * 0x11 >> 1; /* FIX: 0..0x80 */
        qword_t *p = blend_states + 4 * i;
        p[0].dw[0] = GIF_SET_TAG(3, 1, 0, 0, GIF_FLG_PACKED, 1);
        p[0].dw[1] = GIF_REG_AD;
        p[1].dw[0] = GS_SET_ALPHA(0, add ? 2 : 1, as ? 0 : 2, 1, a);
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

/* the prebuilt chain of a model's opaque groups (xlu 0) or of its blended ones (xlu 1); NULL when it has none */
static packet2_t *build_chain_pass(Model *md, int xlu)
{
    u32 batches = 0, groups = 0;
    for (u32 g = 0; g < md->vu_ngroups; g++)
        if ((group_mode(&md->vu[g]) >= 2) == xlu) {
            batches += md->vu[g].nbatch;
            groups++;
        }
    if (!groups)
        return NULL;
    u32 qw = batches * 9 + groups * 4 + 4;
    packet2_t *pk = packet2_create(qw, P2_TYPE_NORMAL, P2_MODE_CHAIN, 1);
    mem_charge(MEM_RENDER, qw * 16);
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        const VuGroup *vg = &md->vu[g];
        int mode = group_mode(vg);
        if ((mode >= 2) != xlu) continue;
        emit_group(pk, vg);
        const u8 *d = vg->data;
        for (u32 b = 0; b < vg->nbatch; b++) {
            u32 ne = vg->batch[2 * b], nu = vg->batch[2 * b + 1];
            add_header(pk, HDRS(mode, ne), nu);
            add_batch(pk, &d, ne, nu, vg->tex != NO_TEX, 2);
            packet2_utils_vu_add_start_program(pk, 0);
        }
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

static void flush_vif(void)
{
    /* FLUSH: the chain only ends once VU1 has finished and PATH1 has delivered its last XGKICK, so whatever is
     * sent on PATH3 afterwards (the HUD) lands on top */
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flush(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0); /* two VIF codes keep the CNT tag 16-byte aligned */
    packet2_chain_close_tag(vif[cur]);
    packet2_utils_vu_add_end_tag(vif[cur]);
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
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
/* a prebuilt chain for one instance: FLUSHE (the previous instance stops reading VU mem 1..4), its matrix, CALL */
static void call_chain(void *chain, float *m)
{
    if (packet2_get_qw_count(vif[cur]) + 8 > PACKET_QW)
        flush_vif();
    packet2_chain_open_cnt(vif[cur], 0, 0, 0);
    packet2_vif_flushe(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_chain_close_tag(vif[cur]);
    packet2_utils_vu_add_unpack_data(vif[cur], 1, m, 4, 0);
    packet2_chain_call(vif[cur], ((packet2_t *)chain)->base, 0, 0, 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_vif_nop(vif[cur], 0);
    packet2_chain_close_tag(vif[cur]);
    cur_tex = -1; /* the chain changed the texture state */
}

/* blended instances of the frame, drawn after everything opaque (render_end), farthest first. ponytail: sorted per
 * instance, not per triangle; skinned models blend in place */
#define MAX_XLU 128
static struct { const Model *md; float *m; float depth; } xlu[MAX_XLU];
static int nxlu;

static void draw_model(const Model *md, const float world[16], const float (*drw)[16])
{
    if (!md->vu_ngroups)
        return;
    float mv[16], pvw[16];
    mat_mul(mv, view, world);
    float rad = md->radius * sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
    if (mv[14] + rad < NEAR || mv[14] - rad > FAR) /* whole model behind the camera or past the far plane */
        return;
    /* side planes through the eye: f x = (w/2) z etc., normals unit (frustum[]); outside by more than rad: culled */
    if (frustum[0] * fabsf(mv[12]) - frustum[1] * mv[14] > rad || frustum[2] * fabsf(mv[13]) - frustum[3] * mv[14] > rad)
        return;
    mat_mul(pvw, proj, mv);
    if (!drw && (md->chain || md->chain_xlu)) {
        if (nmats >= MAX_INST)
            return;
        model_textures(md);
        float *m = mats[nmats++];
        memcpy(m, pvw, 64);
        scale_cols(m, md->qscale);
        if (md->chain)
            call_chain(md->chain, m);
        if (md->chain_xlu && nxlu < MAX_XLU) { /* blended groups wait for the end of the frame, far ones first */
            int k = nxlu++;
            for (; k > 0 && xlu[k - 1].depth < mv[14]; k--)
                xlu[k] = xlu[k - 1];
            xlu[k].md = md; xlu[k].m = m; xlu[k].depth = mv[14];
        }
        for (u32 g = 0; g < md->vu_ngroups; g++)
            tris_drawn += md->vu[g].nverts / 3;
        return;
    }
    u32 need = drw ? 0 : 1, batches = 0;
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        batches += md->vu[g].nbatch;
        if (drw)
            need += md->skin->groups[g].nmat;
    }
    if (nmats + need > MAX_INST)
        return;
    if (packet2_get_qw_count(vif[cur]) + 8 + batches * 10 + md->vu_ngroups * 2 > PACKET_QW)
        flush_vif();
    float (*gm)[16] = mats + nmats; /* this group's matrices */
    u32 m = 1;
    if (!drw) {
        memcpy(gm, pvw, 64);
        scale_cols(gm[0], md->qscale);
        nmats++;
    }
    for (u32 g = 0; g < md->vu_ngroups; g++) {
        const VuGroup *vg = &md->vu[g];
        if (drw) {
            const SkGroup *sg = &md->skin->groups[g];
            gm = mats + nmats;
            m = sg->nmat;
            for (u32 i = 0; i < m; i++) {
                mat_mul(gm[i], pvw, drw[sg->drw[i]]);
                scale_cols(gm[i], md->qscale);
            }
            nmats += m;
        }
        set_tex(vg->tex);
        if (vg->blend) {
            emit_group(vif[cur], vg);
            cur_tex = -1; /* the next group's texture packet puts the opaque state back */
        }
        int mode = group_mode(vg);
        u32 base = 2 + 4 * m;
        const u8 *d = vg->data;
        for (u32 b = 0; b < vg->nbatch; b++) {
            u32 ne = vg->batch[2 * b], nu = vg->batch[2 * b + 1];
            add_header(vif[cur], HDR(mode, m, ne), nu);
            packet2_utils_vu_add_unpack_data(vif[cur], 2, gm, 4 * m, 1);
            add_batch(vif[cur], &d, ne, nu, vg->tex != NO_TEX, base);
            packet2_utils_vu_add_start_program(vif[cur], 0);
            tris_drawn += ne / 3;
        }
    }
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
    packet2_add_2x_s64(vif[cur], GS_SET_FRAME(fb, fw, GS_PSM_32, 0xFFFFFFFF), GS_REG_FRAME);
    packet2_add_2x_s64(vif[cur], GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ZTEST_METHOD_ALLPASS), GS_REG_TEST);
    packet2_add_2x_s64(vif[cur], GS_SET_ZBUF(zbuf.address >> 11, zbuf.zsm, 0), GS_REG_ZBUF);
    packet2_add_2x_s64(vif[cur], GS_SET_PRIM(PRIM_SPRITE, 0, 0, 0, 0, 0, 0, 0, 0), GS_REG_PRIM);
    packet2_add_2x_s64(vif[cur], GS_SET_RGBAQ(0, 0, 0, 0x80, 0x3F800000), GS_REG_RGBAQ);
    packet2_add_2x_s64(vif[cur], GS_SET_XYZ((2048 - SCREEN_W / 2) << 4, (2048 - SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    packet2_add_2x_s64(vif[cur], GS_SET_XYZ((2048 + SCREEN_W / 2) << 4, (2048 + SCREEN_H / 2) << 4, 0), GS_REG_XYZ2);
    packet2_add_2x_s64(vif[cur], GS_SET_FRAME(fb, fw, GS_PSM_32, 0), GS_REG_FRAME);
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

void render_skinned(const Model *md, const float world[16], const float (*drw)[16])
{
    draw_model(md, world, drw);
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
    r.v0.z = r.v1.z = 0xFFFFFF;
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
    q.v0.z = q.v1.z = 0xFFFFFF;
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
        if (packet2_get_qw_count(vif[cur]) + 16 + TEXP + 4 + (j - i) * 5 > PACKET_QW) /* state, its texture change */
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
            float k = 2048.0f / zv, sx = 2048.0f + proj[0] * xv * k, sy = 2048.0f + proj[5] * yv * k;
            float hx = proj[0] * s->half * k, hy = -proj[5] * s->half * k;
            /* wider than the screen (a big emitter's scale, or right at the camera): skipped. Its corners would leave
             * the GS's 0..4095 range and wrap, and a few such sprites stall the GS for seconds (ambient effects) */
            if (hx > SCREEN_W || hy > SCREEN_H)
                continue;
            if (sx + hx < 2048 - SCREEN_W / 2 || sx - hx > 2048 + SCREEN_W / 2 || sy + hy < 2048 - SCREEN_H / 2 ||
                sy - hy > 2048 + SCREEN_H / 2)
                continue;
            u32 z = (u32)(((proj[10] * zv + proj[14]) / zv + 1.0f) * (float)(0xFFFFFF / 2));
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

/* WaterPlant strips: a triangle strip of 8 points each, alpha blended, textured (UV), no depth write; a strip with a
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
            sx[v] = 2048.0f + proj[0] * xv * k;
            sy[v] = 2048.0f + proj[5] * yv * k;
            if (sx[v] < 2048 - SCREEN_W || sx[v] > 2048 + SCREEN_W || sy[v] < 2048 - SCREEN_H || sy[v] > 2048 + SCREEN_H) ok = 0;
            sz[v] = (u32)(((proj[10] * zv + proj[14]) / zv + 1.0f) * (float)(0xFFFFFF / 2));
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
        packet2_add_2x_s64(vif[cur], GS_SET_ALPHA(0, 1, 0, 1, 0), GS_REG_ALPHA);
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

u32 render_end(void)
{
    u32 c0 = cycles();
    for (int i = 0; i < nxlu; i++) {
        model_textures(xlu[i].md);
        call_chain(xlu[i].md->chain_xlu, xlu[i].m);
    }
    nxlu = 0;
    flush_vif();
    dma_channel_wait(DMA_CHANNEL_VIF1, 0);
    if (text_open) {
        packet2_update(gif, draw_enable_tests(gif->next, 0, &zbuf));
        packet2_update(gif, draw_finish(gif->next));
        dma_channel_send_packet2(gif, DMA_CHANNEL_GIF, 1);
        draw_wait_finish();
        text_open = 0;
    }
    u32 busy = cycles() - c0;
    graph_wait_vsync();
    graph_set_framebuffer(0, frame[back].address, SCREEN_W, GS_PSM_32, 0, 0);
    back ^= 1;
    return busy;
}

u32 render_tris_drawn(void) { return tris_drawn; }

void render_tex_stats(u32 *misses, u32 *overflow, u32 *syncs)
{
    *misses = tex_misses; *overflow = tex_overflow; *syncs = tex_syncs;
}
