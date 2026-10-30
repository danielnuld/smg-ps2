/* anim.c against tools/skin.py: Mario's draw matrices for the player's clips (fixture from tools/anim_fixture.py),
 * evaluated from the blob's zlib clips. */
// SRCS: ../src/blob.c ../src/mem.c -Ifake -lz
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/anim.h"
#include "../src/render.h"

int seq_start_id(int *zone) { (void)zone; return -1; } /* blob.c scene_start: the default start */

/* render.c's matrix helpers (that file needs ps2sdk) */
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
void mat_translate_rot_scale(float m[16], const float pos[3], const float rot[3], const float scale[3])
{
    const float d = 3.14159265f / 180.0f;
    float cx = cosf(rot[0] * d), sx = sinf(rot[0] * d), cy = cosf(rot[1] * d), sy = sinf(rot[1] * d);
    float cz = cosf(rot[2] * d), sz = sinf(rot[2] * d);
    mat_identity(m);
    m[0] = cz * cy * scale[0]; m[1] = sz * cy * scale[0]; m[2] = -sy * scale[0];
    m[4] = (cz * sy * sx - sz * cx) * scale[1]; m[5] = (sz * sy * sx + cz * cx) * scale[1]; m[6] = cy * sx * scale[1];
    m[8] = (cz * sy * cx + sz * sx) * scale[2]; m[9] = (sz * sy * cx - cz * sx) * scale[2]; m[10] = cy * cx * scale[2];
    m[12] = pos[0]; m[13] = pos[1]; m[14] = pos[2];
}

static unsigned char *load(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("missing %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(*n + 64);
    unsigned char *a = (unsigned char *)(((size_t)b + 63) & ~(size_t)63);
    if (fread(a, 1, *n, f) != (size_t)*n) exit(1);
    fclose(f);
    return a;
}

int main(void)
{
    long nb, nf;
    unsigned char *blob = load("/mnt/d/smg-ps2/out/slice/hd2.bin", &nb);
    unsigned char *fx = load("fixtures/anim_expected.bin", &nf);
    Scene s;
    memset(&s, 0, sizeof s);
    if (blob_load(&s, blob) != 0) { printf("bad blob\n"); return 1; }
    const Model *m = scene_model(&s, "Mario");
    if (!m || !m->skin) { printf("no skinned Mario\n"); return 1; }
    anim_init(&s);
    static Anim a;
    anim_bind(&a, m->skin);
    unsigned n = *(unsigned *)fx, bad = 0;
    unsigned char *p = fx + 4;
    float worst = 0;
    for (unsigned i = 0; i < n; i++) {
        const char *clip = (const char *)p;
        float frame = *(float *)(p + 32);
        unsigned nd = *(unsigned *)(p + 36);
        const float *want = (const float *)(p + 40);
        p += 40 + nd * 64;
        if (!anim_play(&a, clip, 1.0f, 0, 0)) { printf("clip %s missing\n", clip); return 1; }
        a.frame = frame - 1.0f; /* anim_update advances one frame, then poses */
        a.done = 0;
        anim_update(&a);
        const float (*drw)[16] = anim_drw(&a);
        float e = 0;
        for (unsigned d = 0; d < nd; d++)
            for (int k = 0; k < 16; k++) {
                float tol = k >= 12 ? 0.05f : 1e-3f; /* translation in units, rotation part unitless */
                float diff = fabsf(drw[d][k] - want[d * 16 + k]);
                if (diff / tol > e) e = diff / tol;
            }
        if (e > worst) worst = e;
        if (e > 1.0f) {
            if (bad++ < 12) printf("  %-14s frame %5.1f: off by %.1f x tolerance\n", clip, frame, e);
        }
    }
    printf("test_anim: %u poses, %u differ (worst %.2f x tolerance)\n", n, bad, worst);
    return bad ? 1 : 0;
}
