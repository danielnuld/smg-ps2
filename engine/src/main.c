#include <kernel.h>
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

extern const u8 slice_blob[];

static Scene scene;

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
    if (blob_load(&scene, slice_blob) != 0) {
        printf("slice: bad blob\n");
        for (;;) {}
    }
    render_load_textures(&scene);
    game_init(&scene, slice_blob);
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
