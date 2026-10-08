/* fx.c on the embedded blob's PTCL (tools/jpa.py pack): a kept launch star light emits and draws, stops when no
 * longer kept and its particles die out; a coin get bursts and ends. */
// SRCS: ../src/blob.c ../src/mem.c -Ifake
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/fx.h"
#include "../src/render.h"

static int drawn;
void render_sprites(const Sprite *sp, int n)
{
    drawn = n;
    for (int i = 0; i < n; i++)
        if (!(sp[i].half > 0.0f) || (sp[i].rgba >> 24) > 0x80) { printf("bad sprite %d\n", i); exit(1); }
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
    printf("test_fx ok\n");
    return 0;
}
