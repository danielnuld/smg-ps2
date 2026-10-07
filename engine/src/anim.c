#include "anim.h"
#include "mem.h"
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

/* Clips stay zlib-compressed in the blob (design decision 5) and are inflated on play into a few cache slots
 * (least recently started is replaced). Keys: J3DGetKeyFrameInterpolation / JMAHermiteInterpolation
 * (J3DAnimation.cpp:512-602): type 0 keys are (time, value, tangent), type 1 (time, value, in, out); a track with
 * count 0 is the default (scale 1, rotation 0, translation 0), count 1 a constant. Rotations are s16 << dec_shift,
 * 0x8000 = 180 degrees. Joint local = T * Rz * Ry * Rx * S. */

#define SLOTS 4

static const Scene *sc;
static u8 *slot_data[SLOTS];
static int slot_clip[SLOTS], slot_age[SLOTS], age;
static u32 slot_size;

void anim_init(const Scene *s)
{
    sc = s;
    for (u32 i = 0; i < s->nclips; i++)
        if (s->clips[i].raw > slot_size)
            slot_size = s->clips[i].raw;
    slot_size = (slot_size + 15) & ~15u;
    for (int i = 0; i < SLOTS; i++) {
        slot_data[i] = mem_alloc(MEM_PLAYER, slot_size);
        slot_clip[i] = -1;
    }
}

u32 anim_cache_bytes(void) { return SLOTS * slot_size; }

static const u8 *clip_data(int id)
{
    int lru = 0;
    for (int i = 0; i < SLOTS; i++) {
        if (slot_clip[i] == id) {
            slot_age[i] = ++age;
            return slot_data[i];
        }
        if (slot_age[i] < slot_age[lru])
            lru = i;
    }
    uLongf len = slot_size;
    if (uncompress(slot_data[lru], &len, sc->clips[id].z, sc->clips[id].zsize) != Z_OK) {
        printf("anim: inflate failed for %s\n", sc->clips[id].name);
        return NULL;
    }
    slot_clip[lru] = id;
    slot_age[lru] = ++age;
    return slot_data[lru];
}

void anim_bind(Anim *a, const Skin *skin)
{
    memset(a, 0, sizeof *a);
    a->skin = skin;
    a->clip_id = -1;
    anim_update(a);
}

int anim_play(Anim *a, const char *name, float speed, int loop)
{
    a->speed = speed;
    a->loop = loop;
    for (u32 i = 0; i < sc->nclips; i++)
        if (strcmp(sc->clips[i].name, name) == 0) {
            if ((int)i == a->clip_id)
                return 1;
            a->clip_id = i;
            a->frame = 0;
            a->done = 0;
            return 1;
        }
    return 0;
}

int anim_length(const Anim *a)
{
    return a->clip ? *(const u16 *)a->clip : 0;
}

static float hermite(float f, float t0, float v0, float m0, float t1, float v1, float m1)
{
    float d = t1 - t0, t = (f - t0) / d, t2 = t * t;
    return v0 + t * d * m0 + t2 * (3 * (v1 - v0) - 2 * d * m0 - d * m1) + t2 * t * (2 * (v0 - v1) + d * m0 + d * m1);
}

/* the same key search for f32 and s16 tracks */
#define KEY_FN(NAME, T)                                                                                     \
    static float NAME(float f, u32 count, u32 type, const T *k)                                             \
    {                                                                                                       \
        if (count == 1)                                                                                     \
            return k[0];                                                                                    \
        u32 st = type == 0 ? 3 : 4, last = (count - 1) * st;                                                \
        if (f < k[0])                                                                                       \
            return k[1];                                                                                    \
        if (k[last] <= f)                                                                                   \
            return k[last + 1];                                                                             \
        u32 i = 0;                                                                                          \
        while (i + st <= last && k[i + st] <= f)                                                            \
            i += st;                                                                                        \
        return hermite(f, k[i], k[i + 1], type == 0 ? k[i + 2] : k[i + 3], k[i + st], k[i + st + 1], k[i + st + 2]); \
    }
KEY_FN(key_f, float)
KEY_FN(key_s, s16)

static void clip_local(const u8 *c, float f, u32 j, float scale[3], float rot[3], float trans[3])
{
    const u16 *h = (const u16 *)c;
    u32 shift = c[3], nj = h[2], ns = h[3], nr = h[4];
    const u16 *tab = h + 8;
    u32 o = 16 + nj * 54;
    o = (o + 3) & ~3u;
    const float *sd = (const float *)(c + o);
    o += 4 * ns;
    const s16 *rd = (const s16 *)(c + o);
    o += 2 * nr;
    o = (o + 3) & ~3u;
    const float *td = (const float *)(c + o);
    for (u32 ax = 0; ax < 3; ax++) {
        const u16 *e = tab + (3 * j + ax) * 9;
        scale[ax] = e[0] ? key_f(f, e[0], e[2], sd + e[1]) : 1.0f;
        rot[ax] = e[3] ? (float)((int)key_s(f, e[3], e[5], rd + e[4]) << shift) * (180.0f / 32768.0f) : 0.0f;
        trans[ax] = e[6] ? key_f(f, e[6], e[8], td + e[7]) : 0.0f;
    }
}

void anim_update(Anim *a)
{
    const Skin *k = a->skin;
    a->clip = a->clip_id >= 0 ? clip_data(a->clip_id) : NULL;
    if (a->clip) {
        float len = *(const u16 *)a->clip;
        a->frame += a->speed;
        if (a->frame >= len) {
            if (a->loop)
                a->frame = fmodf(a->frame, len);
            else {
                a->frame = len;
                a->done = 1;
            }
        }
    }
    for (u32 j = 0; j < k->njoints && j < ANIM_MAX_JOINTS; j++) {
        const SkJoint *jt = &k->joints[j];
        float s[3], r[3], t[3], local[16];
        if (a->clip && j < ((const u16 *)a->clip)[2])
            clip_local(a->clip, a->frame, j, s, r, t);
        else
            for (int i = 0; i < 3; i++) {
                s[i] = jt->scale[i];
                r[i] = jt->rot[i] * (180.0f / 3.14159265f);
                t[i] = jt->trans[i];
            }
        mat_translate_rot_scale(local, t, r, s);
        if (jt->parent < 0)
            memcpy(a->world[j], local, 64);
        else
            mat_mul(a->world[j], a->world[jt->parent], local);
    }
    for (u32 d = 0; d < k->ndrw && d < ANIM_MAX_DRW; d++) {
        const SkDrw *dr = &k->drw[d];
        if (!dr->count) {
            memcpy(a->drw[d], a->world[dr->first], 64);
            continue;
        }
        float *out = a->drw[d];
        memset(out, 0, 64);
        for (u32 i = 0; i < dr->count; i++) {
            const SkMix *mx = &k->mix[dr->first + i];
            float ib[16], m[16];
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++)
                    ib[c * 4 + r] = mx->invbind[r * 4 + c];
            ib[3] = ib[7] = ib[11] = 0.0f;
            ib[15] = 1.0f;
            mat_mul(m, a->world[mx->joint], ib);
            for (int q = 0; q < 16; q++)
                out[q] += mx->weight * m[q];
        }
    }
}
