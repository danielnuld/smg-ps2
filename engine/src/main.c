#include <kernel.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>

#include "blob.h"
#include "game.h"
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
    game_init(&scene, slice_blob);
    mem_report();

    u32 t_logic = 0, t_draw = 0, worst = 0, frame = 0;
    char line[128], a[24], b[24], c[24];
    for (;;) {
        u32 t0 = cycles();
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
        ms(c, sizeof c, "WORST", worst);
        snprintf(line, sizeof line, "%s  %s  %s MS  TRIS %u", a, b, c, render_tris_drawn());
        render_text(16, 12, line);
        snprintf(line, sizeof line, "COINS %d  PURPLE %d/%d  PIECES %d  KURIBO %d  RUNTIME %u KB", game_coins(),
                 game_purple(), game_purple_total(), game_starpieces(), game_kuribo_alive(), mem_total() >> 10);
        render_text(16, 26, line);
        if ((frame++ & 255) == 0)
            printf("frame %u: logic %u us, draw %u us, worst %u us, tris %u\n", frame, (u32)(t_logic / EE_MHZ),
                   (u32)(t_draw / EE_MHZ), (u32)(worst / EE_MHZ), render_tris_drawn());
        render_end();
    }
    return 0;
}
