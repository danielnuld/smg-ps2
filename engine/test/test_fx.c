/* fx.c on the embedded blob's PTCL (tools/jpa.py pack): a kept launch star light emits and draws, stops when no
 * longer kept and its particles die out; a coin get bursts and ends. */
// SRCS: ../src/blob.c ../src/mem.c -Ifake
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fx.h"
#include "../src/render.h"

int seq_start_id(int *zone) { (void)zone; return -1; } /* blob.c scene_start: the default start */

void mat_identity(float m[16])
{
    for (int i = 0; i < 16; i++) m[i] = i % 5 == 0;
}

void mat_mul(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int k = 0; k < 4; k++)
            r[4 * c + k] = a[k] * b[4 * c] + a[4 + k] * b[4 * c + 1] + a[8 + k] * b[4 * c + 2] + a[12 + k] * b[4 * c + 3];
    for (int i = 0; i < 16; i++) out[i] = r[i];
}

static int drawn;
int render_sprite_place(const float pos[3], float half, float out[5]) { (void)pos; (void)half; (void)out; return 1; }
void render_sprites(const Sprite *sp, int n)
{
    drawn = n;
    for (int i = 0; i < n; i++)
        if (!(sp[i].half > 0.0f) || (sp[i].rgba >> 24) > 0x80) { printf("bad sprite %d\n", i); exit(1); }
}

void render_mesh_c(const float (*pos)[3], const float (*uv)[2], int nvert, const u16 *idx, int ntri, u16 tex, int blend,
                   u32 rgba)
{
    (void)pos; (void)uv; (void)nvert; (void)idx; (void)ntri; (void)tex; (void)blend; (void)rgba;
}

static unsigned char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n + 64);
    unsigned char *a = (unsigned char *)(((size_t)b + 63) & ~(size_t)63);
    if (fread(a, 1, n, f) != (size_t)n) exit(1);
    fclose(f);
    return a;
}

int main(void)
{
    Scene s;
    memset(&s, 0, sizeof s);
    if (blob_load(&s, load("/mnt/d/smg-ps2/out/slice/hd2.bin")) != 0 || !s.ptcl) { printf("no PTCL\n"); return 1; }
    fx_init(&s);
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 200, 300, 1};
    static int key;
    for (int f = 0; f < 90; f++) {
        fx_keep(FX_SUPERSPINDRIVER_SUPERSPINDRIVERLIGHT, &key, m);
        fx_update();
    }
    fx_draw();
    printf("launch star light: %u particles, %d drawn\n", fx_count(), drawn);
    if (!fx_count() || !drawn) return 1;
    for (int f = 0; f < 400; f++) fx_update(); /* not kept: stops, its particles die */
    if (fx_count()) { printf("light left %u particles\n", fx_count()); return 1; }
    fx_emit(FX_COIN_COINGET, m);
    int peak = 0;
    for (int f = 0; f < 200; f++) {
        fx_update();
        if ((int)fx_count() > peak) peak = fx_count();
    }
    printf("coin get: peak %d, then %u\n", peak, fx_count());
    if (peak < 2 || fx_count()) return 1;
    fx_keep(-1, &key, m); /* no such effect (a row that is not an effect object): nothing, not defs[-1] */
    fx_emit(-1, m);
    for (int f = 0; f < 10; f++) fx_update();
    if (fx_count()) { printf("effect -1 made %u particles\n", fx_count()); return 1; }
    printf("test_fx ok\n");
    return 0;
}
