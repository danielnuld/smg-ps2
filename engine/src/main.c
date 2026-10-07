#include <kernel.h>
#include <malloc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

#include "blob.h"
#include "camera.h"
#include "game.h"
#include "mem.h"
#include "pad.h"
#include "render.h"

#include "packs_cfg.h" /* written by build.sh: PACKS = "<Galaxy>/<scenario>", or empty */

extern const u8 slice_blob[];

static Scene scene;

#ifdef PACKS
/* Streaming layout (tools/slice.py packs, task 5.1) read through host: (PCSX2 host filesystem, the ELF's folder).
 * At boot the base packs; the stage packs come with the first zone swap (stream_update). */
static u32 pack_bytes;

static const u8 *load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("packs: missing %s\n", path);
        for (;;) {}
    }
    fseek(f, 0, SEEK_END);
    u32 n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *b = memalign(64, n);
    if (!b || fread(b, 1, n, f) != n) {
        printf("packs: cannot read %s (%u B)\n", path, n);
        for (;;) {}
    }
    fclose(f);
    pack_bytes += n;
    return b;
}

static const u8 *zone_files[64];      /* the stage packs in use, one per zone */
static unsigned char zone_full[64];    /* 1: that stage's .bin is in use, 0: its .low.bin */
static const u8 *scene_blob;
static u32 nzones, base_models, base_tex, base_kcl;
static MemMark base_mark;

/* Next zone's packs, read a slice per frame (a launch star's flight hides it); stages whose pack does not change
 * keep their buffer. ponytail: the old and the new packs are both in RAM until the swap */
#define READ_STEP (32 << 10) /* ~4.5 ms of host: read per frame; 3 MB fits a 300-frame flight */
static struct {
    int zone, active;                  /* target zone */
    u32 z;                             /* next stage to read */
    unsigned char full[64];
    const u8 *buf[64];
    FILE *f;
    u8 *cur;
    u32 size, off, bytes;
} pend;

static void chain_of(int zone, unsigned char *full)
{
    u32 sz;
    const s32 *parent = (const s32 *)(blob_chunk(scene_blob, "ZPAR", &sz) + 4);
    memset(full, 0, 64);
    for (int z = zone; z >= 0 && z < 64; z = parent[z])
        full[z] = 1;
    full[0] = 1;
}

static void pend_cancel(void)
{
    if (pend.f) fclose(pend.f);
    if (pend.cur) free(pend.cur);
    for (u32 z = 0; z < pend.z && z < nzones; z++)
        if (pend.buf[z] && pend.buf[z] != zone_files[z]) free((void *)pend.buf[z]);
    memset(&pend, 0, sizeof pend);
}

static void pend_start(int zone)
{
    pend_cancel();
    pend.zone = zone;
    pend.active = 1;
    chain_of(zone, pend.full);
}

/* Read up to `budget` bytes of the pending packs; returns 1 when every stage pack is in memory. */
static int pend_step(u32 budget)
{
    char path[160];
    u32 sz;
    const u8 *zn = blob_chunk(scene_blob, "ZONE", &sz);
    while (pend.z < nzones) {
        u32 z = pend.z;
        if (!pend.f && !pend.cur) {
            if (zone_files[z] && pend.full[z] == zone_full[z]) { /* unchanged: keep it */
                pend.buf[z] = zone_files[z];
                pend.z++;
                continue;
            }
            snprintf(path, sizeof path, "host:packs/%s/%s%s", PACKS, (const char *)(zn + 4 + 56 * z),
                     pend.full[z] ? ".bin" : ".low.bin");
            pend.f = fopen(path, "rb");
            if (!pend.f) {
                printf("packs: missing %s" "\n", path);
                for (;;) {}
            }
            fseek(pend.f, 0, SEEK_END);
            pend.size = ftell(pend.f);
            fseek(pend.f, 0, SEEK_SET);
            pend.cur = memalign(64, pend.size);
            pend.off = 0;
        }
        if (!budget)
            return 0;
        u32 n = pend.size - pend.off < budget ? pend.size - pend.off : budget;
        if (fread(pend.cur + pend.off, 1, n, pend.f) != n) {
            printf("packs: read error" "\n");
            for (;;) {}
        }
        pend.off += n;
        pend.bytes += n;
        budget -= n;
        if (pend.off == pend.size) {
            fclose(pend.f);
            pend.f = NULL;
            pend.buf[z] = pend.cur;
            pend.cur = NULL;
            pend.z++;
        }
    }
    return 1;
}

/* Swap to the pending zone (its packs all read): drop the zone models, textures and chains, parse the packs. */
static void pend_commit(Scene *s)
{
    u32 t0 = cycles();
    render_release(s, base_tex, base_models);
    for (u32 z = 0; z < nzones; z++) {
        if (zone_files[z] && zone_files[z] != pend.buf[z]) free((void *)zone_files[z]);
        zone_files[z] = pend.buf[z];
    }
    memcpy(zone_full, pend.full, sizeof zone_full);
    s->nmodels = base_models;
    s->ntex = base_tex;
    s->nkcl = base_kcl;
    mem_release(&base_mark);
    for (u32 z = 0; z < nzones; z++)
        if (blob_add(s, zone_files[z]) != 0) {
            printf("packs: zone %d failed" "\n", pend.zone);
            for (;;) {}
        }
    render_load_textures(s, base_tex, base_models);
    game_rebind();
    printf("packs: zone %d, %u models, %u textures, %u KB read before, swap %u us" "\n", pend.zone, s->nmodels, s->ntex,
           pend.bytes >> 10, (u32)((cycles() - t0) / 294.912f));
    memset(&pend, 0, sizeof pend);
}

/* Per frame: prepare the zone the game expects next, swap when it is wanted (reading the rest at once if needed). */
static void stream_update(Scene *s)
{
    int next = game_zone_next(zone_full), want = game_zone_wanted(zone_full);
    if (want >= 0 && (!pend.active || pend.zone != want))
        pend_start(want);
    else if (want < 0 && next >= 0 && (!pend.active || pend.zone != next))
        pend_start(next);
    if (!pend.active)
        return;
    if (pend_step(want >= 0 ? 0xFFFFFFFF : READ_STEP) && want >= 0)
        pend_commit(s);
}

static const u8 *load_packs(Scene *s)
{
    char path[160];
    const char *dir = "host:packs/"; /* run the ELF from out/ (host: cannot leave the ELF folder) */
    snprintf(path, sizeof path, "%s%s/scene.bin", dir, PACKS);
    scene_blob = load_file(path);
    if (blob_load(s, scene_blob) != 0)
        return NULL;
    snprintf(path, sizeof path, "%sresident.bin", dir);
    if (blob_add(s, load_file(path)) != 0)
        return NULL;
    snprintf(path, sizeof path, "%s%s/common.bin", dir, PACKS);
    if (blob_add(s, load_file(path)) != 0)
        return NULL;
    u32 sz;
    nzones = *(const u32 *)blob_chunk(scene_blob, "ZONE", &sz);
    if (nzones > 64)
        return NULL;
    base_models = s->nmodels;
    base_tex = s->ntex;
    base_kcl = s->nkcl;
    printf("packs: %s, %u zones, base %u models, %u textures, %u KB read\n", PACKS, nzones, s->nmodels, s->ntex,
           pack_bytes >> 10);
    return scene_blob;
}
#endif

#define EE_MHZ 294.912f

static void load_modules(void)
{
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void ms(char *out, int n, const char *label, u32 c)
{
    u32 us = (u32)(c / EE_MHZ);
    snprintf(out, n, "%s %u.%02u", label, us / 1000, (us % 1000) / 10);
}

int main(void)
{
    load_modules();
    render_init();
    pad_init();
#ifdef PACKS
    const u8 *blob = load_packs(&scene);
#else
    const u8 *blob = blob_load(&scene, slice_blob) == 0 ? slice_blob : NULL;
#endif
    if (!blob) {
        printf("slice: bad blob\n");
        for (;;) {}
    }
#ifdef PACKS
    /* base packs first, then the game (its allocations stay), then the zone packs above the mark */
    render_load_textures(&scene, 0, 0);
    game_init(&scene, blob);
    mem_mark(&base_mark);
    pend_start(scene_start(&scene)->zone);
    pend_step(0xFFFFFFFF);
    pend_commit(&scene);
#else
    render_load_textures(&scene, 0, 0);
    game_init(&scene, blob);
#endif
    mem_report();

    u32 t_logic = 0, t_draw = 0, t_gs = 0, worst = 0, frame = 0;
#ifdef BENCH
    /* frames 60..1859 of the scripted path: sums and worsts of logic (minus KCL), KCL, draw, GS finish, total */
    u64 sum[5] = {0};
    u32 top[5] = {0};
#endif
    char line[128], a[24], b[24], c[24];
    for (;;) {
        u32 t0 = cycles();
        game_kcl_cycles = 0;
        pad_update();
        game_update();
#ifdef PACKS
#ifdef BOX_TEST
        if (frame == 120)
            printf("box test: Mario spins on box row %d\n", game_test_box());
        if (frame == 240)
            printf("box test: coins %d\n", game_coins());
#endif
#ifdef ZONE_TEST
        if (frame == 120)
            printf("zone test: launch star %d\n", game_test_launch(0));
        { /* per 60 frames: frames longer than 20 ms (missed vsync) and the worst logic / draw / GS of the window */
            static u32 prev, slow, wl, wd, wg;
            if (prev && t0 - prev > (u32)(20000 * EE_MHZ)) slow++;
            prev = t0;
            if (t_logic > wl) wl = t_logic;
            if (t_draw > wd) wd = t_draw;
            if (t_gs > wg) wg = t_gs;
            if (frame % 60 == 0 && frame <= 900) {
                char d[128];
                game_debug(d, sizeof d);
                printf("zone test f %u: slow %u, worst logic %u draw %u gs %u us; %s\n", frame, slow,
                       (u32)(wl / EE_MHZ), (u32)(wd / EE_MHZ), (u32)(wg / EE_MHZ), d);
                slow = wl = wd = wg = 0;
            }
        }
#endif
        stream_update(&scene);
#endif
        u32 t1 = cycles();
        game_draw();
        u32 t2 = cycles();
        t_logic = t1 - t0;
        t_draw = t2 - t1;
        if (t2 - t0 > worst && frame > 60) worst = t2 - t0;
        ms(a, sizeof a, "LOGIC", t_logic);
        ms(b, sizeof b, "DRAW", t_draw);
        ms(c, sizeof c, "GS", t_gs);
        snprintf(line, sizeof line, "%s  %s  %s", a, b, c);
        ms(a, sizeof a, "WORST", worst);
        snprintf(line + strlen(line), sizeof line - strlen(line), "  %s MS  TRIS %u", a, render_tris_drawn());
        render_text(16, 12, line);
        snprintf(line, sizeof line, "COINS %d  PURPLE %d/%d  PIECES %d  KURIBO %d  RUNTIME %u KB", game_coins(),
                 game_purple(), game_purple_total(), game_starpieces(), game_kuribo_alive(), mem_total() >> 10);
        render_text(16, 26, line);
        snprintf(line, sizeof line, "CAM %d %s", camera_current(), camera_type_name());
        render_text(16, 40, line);
        if ((frame & 255) == 0) {
            char dbg[128];
            game_debug(dbg, sizeof dbg);
            printf("%s\n", dbg);
        }
        if ((frame++ & 255) == 0)
            printf("frame %u: logic %u us, draw %u us, worst %u us, tris %u\n", frame, (u32)(t_logic / EE_MHZ),
                   (u32)(t_draw / EE_MHZ), (u32)(worst / EE_MHZ), render_tris_drawn());
        t_gs = render_end();
#ifdef BENCH
        if (frame > 60 && frame <= 1860) {
            u32 v[5] = {t_logic - game_kcl_cycles, game_kcl_cycles, t_draw, t_gs, t_logic + t_draw + t_gs};
            for (int i = 0; i < 5; i++) {
                sum[i] += v[i];
                if (v[i] > top[i]) top[i] = v[i];
            }
        }
        if (frame == 1861) {
            static const char *nm[5] = {"logic", "kcl", "draw", "gs", "total"};
            for (int i = 0; i < 5; i++)
                printf("bench %-5s avg %u us worst %u us\n", nm[i], (u32)(sum[i] / 1800 / EE_MHZ), (u32)(top[i] / EE_MHZ));
            game_debug(line, sizeof line);
            printf("bench end: %s coins %d purple %d pieces %d kuribo %d\n", line, game_coins(), game_purple(),
                   game_starpieces(), game_kuribo_alive());
            u32 tm, to, ts;
            render_tex_stats(&tm, &to, &ts);
            printf("bench textures: %u uploads, %u without room, %u waits for room\n", tm, to, ts);
        }
#endif
    }
    return 0;
}
