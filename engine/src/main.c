#include <kernel.h>
#include <loadfile.h>
#include <math.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>

#include "blob.h"
#include "mem.h"
#include "pad.h"
#include "render.h"

extern const u8 slice_blob[];

static Scene scene;

/* EE COUNT register: one tick per CPU cycle (294.912 MHz). */
static inline u32 cycles(void)
{
    u32 c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}
#define EE_HZ 294912000.0f

static void load_modules(void)
{
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void colour_for(const char *t, u8 *r, u8 *g, u8 *b)
{
    *r = 0x70; *g = 0xB0; *b = 0x60; /* planet */
    if (t[0] == 'C' && t[1] == 'o') { *r = 0xF0; *g = 0xD0; *b = 0x30; }       /* Coin */
    else if (t[0] == 'P' && t[1] == 'u') { *r = 0xB0; *g = 0x50; *b = 0xF0; }  /* PurpleCoin */
    else if (t[0] == 'S') { *r = 0x60; *g = 0xD0; *b = 0xF0; }                 /* StarPiece */
    else if (t[0] == 'K') { *r = 0x90; *g = 0x50; *b = 0x20; }                 /* Kuribo */
}

int main(void)
{
    load_modules();
    {   /* heap check: how much malloc can give right after boot */
        u32 got = 0;
        for (u32 sz = 16 << 20; sz >= 64 << 10; sz >>= 1) { void *p = malloc(sz); if (p) { got = sz; free(p); break; } }
        printf("slice: largest malloc at boot %u KB\n", got >> 10);
    }
    render_init();
    pad_init();
    if (blob_load(&scene, slice_blob) != 0) {
        printf("slice: bad blob\n");
        for (;;) {}
    }
    printf("slice: %u types, %u objects, %u models, kcl %u B\n", scene.ntypes, scene.nobjs, scene.nmodels,
           scene.kcl_size);

    /* world matrices for every placed object that has a model (zone-local space) */
    const Model **mdl = mem_alloc(MEM_OBJECTS, scene.nobjs * sizeof(Model *));
    float (*world)[16] = mem_alloc(MEM_OBJECTS, scene.nobjs * 64);
    for (u32 i = 0; i < scene.nobjs; i++) {
        const Row *r = &scene.objs[i];
        mdl[i] = scene_model(&scene, scene.types + 32 * r->type);
        mat_translate_rot_scale(world[i], r->pos, r->rot, r->scale);
    }
    mem_report();

    float yaw = 0.6f, pitch = 0.35f, dist = 6500.0f;
    u32 frame_cycles = 0;
    char line[96];
    for (;;) {
        u32 t0 = cycles();
        pad_update();
        yaw += pad_lx() * 0.03f + 0.004f;
        pitch += pad_ly() * 0.02f;
        if (pad_held(PAD_R1)) dist *= 0.98f;
        if (pad_held(PAD_L1)) dist *= 1.02f;
        float eye[3] = {dist * cosf(pitch) * sinf(yaw), dist * sinf(pitch), dist * cosf(pitch) * cosf(yaw)};
        float target[3] = {0, 0, 0}, up[3] = {0, 1, 0};
        render_begin(eye, target, up);
        for (u32 i = 0; i < scene.nobjs; i++) {
            if (!mdl[i])
                continue;
            u8 r, g, b;
            colour_for(scene.types + 32 * scene.objs[i].type, &r, &g, &b);
            render_model(mdl[i], world[i], r, g, b);
        }
        u32 us = (u32)(frame_cycles / (EE_HZ / 1000000.0f));
        snprintf(line, sizeof line, "FRAME %u.%02u MS  TRIS %u  RUNTIME %u KB", us / 1000, (us % 1000) / 10,
                 render_tris_drawn(), mem_total() >> 10);
        render_text(16, 16, line);
        frame_cycles = cycles() - t0; /* EE time before waiting for vsync */
        render_end();
    }
    return 0;
}
