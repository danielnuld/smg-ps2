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
 * Loaded once at boot for the start zone: its chain in full, every other stage low. ponytail: no reload on zone
 * change yet, and the files stay in malloc'd memory. */
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

static const u8 *load_packs(Scene *s)
{
    char path[160];
    const char *dir = "host:packs/"; /* run the ELF from out/ (host: cannot leave the ELF folder) */
    snprintf(path, sizeof path, "%s%s/scene.bin", dir, PACKS);
    const u8 *scene_blob = load_file(path);
    if (blob_load(s, scene_blob) != 0)
        return NULL;
    snprintf(path, sizeof path, "%sresident.bin", dir);
    if (blob_add(s, load_file(path)) != 0)
        return NULL;
    snprintf(path, sizeof path, "%s%s/common.bin", dir, PACKS);
    if (blob_add(s, load_file(path)) != 0)
        return NULL;
    u32 sz;
    const u8 *zn = blob_chunk(scene_blob, "ZONE", &sz), *zp = blob_chunk(scene_blob, "ZPAR", &sz);
    u32 nz = *(const u32 *)zn;
    const s32 *parent = (const s32 *)(zp + 4);
    int chain[64] = {0};
    for (int z = scene_start(s)->zone; z >= 0 && z < 64; z = parent[z])
        chain[z] = 1;
    chain[0] = 1;
    for (u32 z = 0; z < nz && z < 64; z++) {
        snprintf(path, sizeof path, "%s%s/%s%s", dir, PACKS, (const char *)(zn + 4 + 56 * z),
                 chain[z] ? ".bin" : ".low.bin");
        if (blob_add(s, load_file(path)) != 0)
            return NULL;
    }
    printf("packs: %s, %u zones, %u models, %u textures, %u KB read\n", PACKS, nz, s->nmodels, s->ntex,
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
    render_load_textures(&scene);
    game_init(&scene, blob);
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
        }
#endif
    }
    return 0;
}
