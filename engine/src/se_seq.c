/* Sound effects: JAudio2's SE sequencer after Petari (JASSeqParser, JASTrack, JASChannel, JASOscillator,
 * JASBasicInst / JASDrumSet, JAUSeqCollection), playing on SPU2 voices (se.c). The data is tools/sound.py's
 * sound.bin (BSC sequences and IBNK banks as on the Wii, big-endian; the waves as SPU ADPCM with their table).
 * Time: 80-sample sub-frames at the DAC rate (32028.47 Hz, 400.36 a second), 400.36 / 60 of them a game frame;
 * a sequence ticks timebase x tempo / 60 times a second; envelopes advance 48000 / dac x rate a sub-frame. Each
 * frame a live channel's pitch and volume go to its voice (SPU pitch 4096 = 48 kHz).
 * ponytail: no vibrato / tremolo, FIR / IIR, effect buses, interrupts or syncCPU callbacks (0xFFFF); Rand effects
 * at their base; the 3D (JAIAudience) is a plain distance fall-off and pan; voices are taken from the quietest
 * channel when all 24 are busy; envelopes reach the SPU once a frame. */
#include "se.h"

#include <libsd-common.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DAC (32000.0f * 1.0008897f)
#define SUBS_PER_FRAME (DAC / 80.0f / 60.0f)
#define SPU_BASE 0x10000u
#define NVOICE 24
#define MAX_CH 48
#define MAX_TR 192
#define MAX_SND 64

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static s16 bes16(const u8 *p) { return (s16)be16(p); }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static float bef(const u8 *p) { union { u32 u; float f; } v = {be32(p)}; return v.f; }

/* sound.bin */
typedef struct { u16 bank, wsys; u32 off; } BankRow;
typedef struct { u32 id, seq; u8 prio, vol, pad[2]; } SeRow;
typedef struct { u16 wsys, wid; u32 spu, n, loop; float rate; u8 base, pad[3]; } WaveRow;
static const u8 *snd, *bsc;
static const BankRow *banks;
static const SeRow *ses;
static const WaveRow *waves;
static u32 nbanks, nses, nwaves, nbas;
typedef struct { char key[32]; u32 off, n; } BasRow;
static const BasRow *bas;
static const u8 *basent;
static struct { const u8 *ib, *envt, *osct, *list; u16 wsys; } bk[256];

/* envelope data (JASOscillator::Data): a table of BE points {mode, time, value} */
typedef struct { u8 target; float rate, scale, vertex; const u8 *table, *rel; } OscData;
typedef struct { const OscData *d; float t, v, v0, v1; int i, mode, state; u16 dr; } Osc;

typedef struct Track Track;
typedef struct Sound Sound;
typedef struct {
    u8 used, alive, released, drum;
    Track *tr;
    const WaveRow *w;
    float mul, key, sweep, sweep_to, vol;
    int sweep_n, vel, voice, age;
    u32 timer;
    OscData od[2];
    Osc osc[2];
} Chan;
struct Track {
    u8 used, done, wait_note, ready, over_on[2];
    const u8 *seq;
    u32 pc, stack[8], loopn[8];
    int sp, transpose, bend, gate_rate, bank, prog, timebase, tempo, last_key;
    u32 wait;
    u16 regs[14], ports[16], dr;
    float params[6], ramp_to[6], mix_vol, mix_oct;
    u32 ramp_n[6];
    Track *parent, *child[16];
    Chan *notes[8];
    OscData over[2];
    u8 adsr[24];
    Sound *snd;
};
struct Sound {
    u8 used, has_pos, level_seen, stopping;
    u32 id, handle;
    u8 prio;
    float vol, pitch, acc, pos[3], base; /* base: the table's volume */
    Track *root;
};
static Chan ch[MAX_CH];
static Track tr[MAX_TR];
static Sound so[MAX_SND];
static u32 next_handle = 1;
static float listener[3], right[3];
static int voice_of[NVOICE]; /* channel index on each voice, -1 free */
static float sub_acc;
/* JASOscillator's table_list (Petari JASOscillator.cpp): Linear, Square, SqRoot, SampleCell */
static const float curves[4][17] = {
    {1.0f, 0.9375f, 0.875f, 0.8125f, 0.75f, 0.6875f, 0.625f, 0.5625f, 0.5f, 0.4375f, 0.375f, 0.3125f, 0.25f, 0.1875f,
     0.125f, 0.0625f, 0.0f},
    {1.0f, 0.9682459831237793f, 0.9354140162467957f, 0.9013879895210266f, 0.8660249710083008f, 0.8291559815406799f,
     0.790569007396698f, 0.75f, 0.7071070075035095f, 0.66143798828125f, 0.6123719811439514f, 0.55901700258255f, 0.5f,
     0.433012992143631f, 0.35355299711227417f, 0.25f, 0.0f},
    {1.0f, 0.8789060115814209f, 0.765625f, 0.6601560115814209f, 0.5625f, 0.4726560115814209f, 0.390625f,
     0.3164060115814209f, 0.25f, 0.1914059966802597f, 0.140625f, 0.09765619784593582f, 0.0625f, 0.03515620157122612f,
     0.015625f, 0.00390625f, 0.0f},
    {1.0f, 0.9704890251159668f, 0.7812740206718445f, 0.5462809801101685f, 0.39979198575019836f, 0.28931498527526855f,
     0.21210399270057678f, 0.15747599303722382f, 0.1126129999756813f, 0.08178959786891937f, 0.057985201478004456f,
     0.04364150017499924f, 0.03082370012998581f, 0.023712899535894394f, 0.015259300358593464f, 0.00915555004030466f,
     0.0f}};

/* ---- data ---- */
int se_load(const u8 *b)
{
    if (!b || memcmp(b, "SNDB", 4)) { printf("se: no sound.bin\n"); return -1; }
    const u32 *h = (const u32 *)(b + 4);
    snd = b;
    bsc = b + h[0];
    banks = (const BankRow *)(b + h[2]); nbanks = h[3];
    ses = (const SeRow *)(b + h[4]); nses = h[5];
    waves = (const WaveRow *)(b + h[6]); nwaves = h[7];
    bas = (const BasRow *)(b + h[10]); nbas = h[11]; basent = b + h[12];
    for (u32 i = 0; i < nbanks && banks[i].bank < 256; i++) {
        const u8 *ib = b + banks[i].off;
        u32 size = be32(ib + 4);
        bk[banks[i].bank].ib = ib;
        bk[banks[i].bank].wsys = banks[i].wsys;
        for (u32 o = 0x20; o < size; o = (o + 0xB + be32(ib + o + 4)) & ~3u) {
            if (!memcmp(ib + o, "ENVT", 4)) bk[banks[i].bank].envt = ib + o + 8;
            if (!memcmp(ib + o, "OSCT", 4)) bk[banks[i].bank].osct = ib + o;
            if (!memcmp(ib + o, "LIST", 4)) bk[banks[i].bank].list = ib + o;
        }
    }
    se_upload(SPU_BASE, b + h[8], h[9]);
    for (int v = 0; v < NVOICE; v++) voice_of[v] = -1;
    printf("se: %u SEs, %u waves, %u banks, %u KB ADPCM\n", nses, nwaves, nbanks, h[9] >> 10);
    return 0;
}

static const SeRow *se_row(u32 id)
{
    u32 key = id & 0xFFFFFF;
    int lo = 0, hi = (int)nses - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (ses[m].id == key) return &ses[m];
        if (ses[m].id < key) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}
static const WaveRow *wave_row(u16 wsys, u16 wid)
{
    u32 key = (u32)wsys << 16 | wid;
    int lo = 0, hi = (int)nwaves - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        u32 k = (u32)waves[m].wsys << 16 | waves[m].wid;
        if (k == key) return &waves[m];
        if (k < key) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* ---- JASOscillator ---- */
static void osc_init(Osc *o, const OscData *d)
{
    memset(o, 0, sizeof *o);
    o->d = d;
    if (!d) return;
    if (!d->table) { o->state = 2; o->v = 1.0f; return; }
    o->v1 = bes16(d->table + 4) / 32768.0f;
    o->mode = bes16(d->table);
    o->state = 1;
}
static void osc_cur(Osc *o, float len)
{
    float x = len > 0 ? 16.0f * (o->t / len) : 16.0f;
    if (x > 16.0f) x = 16.0f;
    int i = (int)x;
    if (i > 15) i = 15;
    float f = x - i;
    const float *t = curves[o->mode & 3];
    float c = (1.0f - f) * t[i] + f * t[i + 1];
    o->v = o->v0 * c + o->v1 * (1.0f - c);
}
static void osc_update(Osc *o)
{
    if (o->state == 4) {
        float x = (float)(o->dr & 0x3FFF);
        if (o->t < x) osc_cur(o, x);
        else { o->v = o->v1; o->state = 0; }
        return;
    }
    if (o->state == 2) return;
    const u8 *t = o->state == 1 ? o->d->table : o->d->rel;
    if (!t) { o->state = 2; return; }
    for (int guard = 0; guard < 64 && o->t >= bes16(t + 6 * o->i + 2); guard++) {
        o->t -= bes16(t + 6 * o->i + 2);
        o->v = o->v1;
        o->i++;
        o->v0 = o->v;
        int m = bes16(t + 6 * o->i);
        if (m == 0xF) { o->state = 0; return; }
        if (m == 0xE) { o->state = 2; return; }
        if (m == 0xD) o->i = bes16(t + 6 * o->i + 4);
        else { o->mode = m; o->v1 = bes16(t + 6 * o->i + 4) / 32768.0f; }
    }
    osc_cur(o, bes16(t + 6 * o->i + 2));
}
static void osc_inc(Osc *o, float inc)
{
    if (o->state == 0 || o->state == 2) return;
    o->t += inc * o->d->rate;
    osc_update(o);
}
static void osc_release(Osc *o)
{
    const OscData *d = o->d;
    if (!d->rel && !o->dr) { o->state = 0; return; }
    if (o->dr) {
        o->t = 0; o->v0 = o->v; o->v1 = 0; o->i = 0;
        o->mode = (o->dr >> 14) & 3; o->state = 4;
        osc_update(o);
        return;
    }
    if (d->table != d->rel) {
        o->t = 0; o->v0 = o->v; o->v1 = bes16(d->rel + 4) / 32768.0f; o->i = 0;
        o->mode = bes16(d->rel);
    }
    o->state = 3;
    osc_update(o);
}
static float osc_value(const Osc *o) { return o->v * o->d->scale + o->d->vertex; }

/* ---- channels ---- */
static void chan_release(Chan *c, u16 dr)
{
    if (!c || c->released || !c->alive) return;
    c->released = 1;
    int valid = 0;
    for (int k = 0; k < 2; k++)
        if (c->osc[k].d) {
            if (dr) c->osc[k].dr = dr;
            osc_release(&c->osc[k]);
            valid = 1;
        }
    if (!valid) c->alive = 0;
}

/* JASChannel::updateDSPChannel, without the DSP: timers, envelopes, the key sweep */
static void chan_sub(Chan *c)
{
    if (c->timer && --c->timer == 0) chan_release(c, 0);
    for (int k = 0; k < 2; k++) {
        if (!c->osc[k].d) continue;
        osc_inc(&c->osc[k], 48000.0f / DAC);
        if (k == 0 && c->osc[0].state == 0) { c->alive = 0; return; }
    }
    if (c->sweep_n) { c->sweep += (c->sweep_to - c->sweep) / c->sweep_n; c->sweep_n--; }
}

/* JASInstRand (base) / JASInstSense on volume and pitch */
static void effects(const u8 *ib, const u8 *p, int key, int vel, float *vol, float *pitch)
{
    u32 n = be32(p);
    for (u32 k = 0; k < n; k++) {
        const u8 *e = ib + be32(p + 4 + 4 * k);
        float r;
        if (!memcmp(e, "Rand", 4)) r = bef(e + 8);
        else if (!memcmp(e, "Sens", 4)) {
            int x = e[5] == 1 ? vel : key, mp = e[6];
            float a = bef(e + 8), b = bef(e + 12);
            if (mp == 0 || mp == 127) r = a + x * (b - a) / 127.0f;
            else if (x < mp) r = a + (1.0f - a) * x / mp;
            else r = 1.0f + (b - 1.0f) * (x - mp) / (127.0f - mp);
        } else continue;
        if (e[4] == 0) *vol *= r;
        else if (e[4] == 1) *pitch *= r;
    }
}
static const u8 *velo_pick(const u8 *p, int vel) /* the first velocity region with vel <= its high velocity */
{
    u32 n = be32(p);
    for (u32 k = 0; k < n; k++)
        if (vel <= p[4 + 16 * k]) return p + 4 + 16 * k;
    return NULL;
}
static void osc_data(OscData *od, int b, u32 idx)
{
    const u8 *r = bk[b].osct + 12 + 0x1C * idx;
    od->target = r[4];
    od->rate = bef(r + 8);
    od->table = bk[b].envt + be32(r + 12);
    od->rel = bk[b].envt + be32(r + 16);
    od->scale = bef(r + 20);
    od->vertex = bef(r + 24);
}

static Chan *chan_new(void)
{
    for (int i = 0; i < MAX_CH; i++)
        if (!ch[i].used) { memset(&ch[i], 0, sizeof ch[i]); ch[i].used = 1; ch[i].voice = -1; return &ch[i]; }
    return NULL;
}

/* JASTrack::channelStart -> JASBank::noteOn (JASBasicInst / JASDrumSet::getParam) */
static Chan *track_start(Track *t, int key, int vel, u32 timer)
{
    int b = t->bank;
    if (b < 0 || b >= 256 || !bk[b].ib || !bk[b].list) return NULL;
    const u8 *ib = bk[b].ib, *list = bk[b].list;
    if ((u32)t->prog >= be32(list + 8)) return NULL;
    u32 ro = be32(list + 12 + 4 * t->prog);
    if (!ro) return NULL;
    const u8 *r = ib + ro, *vm;
    float vol, pitch;
    int drum = 0, nosc = 0;
    u16 dr = 0;
    u32 oi[2];
    if (!memcmp(r, "Inst", 4)) {
        u32 n = be32(r + 4);
        for (u32 k = 0; k < n && k < 2; k++) oi[nosc++] = be32(r + 8 + 4 * k);
        const u8 *p = r + 8 + 4 * n;
        const u8 *eff = p;
        p += 4 + 4 * be32(p);
        u32 nk = be32(p);
        p += 4;
        const u8 *km = NULL;
        for (u32 k = 0; k < nk; k++) {
            if (!km && key <= p[0]) km = p;
            p += 8 + 16 * be32(p + 4);
        }
        vol = bef(p); pitch = bef(p + 4);
        effects(ib, eff, key, vel, &vol, &pitch);
        if (!km) return NULL;
        vm = velo_pick(km + 4, vel);
    } else if (!memcmp(r, "Perc", 4)) {
        if ((u32)key >= be32(r + 4)) return NULL;
        u32 po = be32(r + 8 + 4 * key);
        if (!po) return NULL;
        const u8 *p = ib + po;
        vol = bef(p + 4); pitch = bef(p + 8); dr = be16(p + 14);
        effects(ib, p + 0x10, key, vel, &vol, &pitch);
        vm = velo_pick(p + 0x10 + 4 + 4 * be32(p + 0x10), vel);
        drum = 1;
    } else return NULL;
    if (!vm) return NULL;
    const WaveRow *w = wave_row(bk[b].wsys, (u16)be32(vm + 4));
    if (!w) return NULL;
    Chan *c = chan_new();
    if (!c) return NULL;
    c->tr = t; c->w = w; c->alive = 1; c->drum = drum;
    c->mul = pitch * bef(vm + 12);
    c->vol = vol * bef(vm + 8);
    c->vel = vel;
    c->key = drum ? 0.0f : (float)(key - w->base);
    c->timer = timer;
    for (int k = 0; k < 2; k++) {
        const OscData *d = NULL;
        if (t->over_on[k]) d = &t->over[k];
        else if (drum && k == 0) { c->od[0].target = 0; c->od[0].rate = 1; c->od[0].scale = 1; d = &c->od[0]; }
        else if (k < nosc) { osc_data(&c->od[k], b, oi[k]); d = &c->od[k]; }
        osc_init(&c->osc[k], d);
        c->osc[k].dr = t->dr ? t->dr : dr;
    }
    return c;
}

/* ---- tracks ---- */
static Track *track_new(Sound *s, Track *parent, u32 pc)
{
    for (int i = 0; i < MAX_TR; i++)
        if (!tr[i].used) {
            Track *t = &tr[i];
            memset(t, 0, sizeof *t);
            t->used = 1; t->snd = s; t->parent = parent; t->seq = bsc; t->pc = pc;
            t->params[0] = 1.0f; t->params[3] = 0.5f;
            t->bend = 12; t->gate_rate = 100; t->last_key = 60;
            t->bank = parent ? parent->bank : 0; t->prog = parent ? parent->prog : 0xF0;
            t->timebase = parent ? parent->timebase : 48; t->tempo = parent ? parent->tempo : 120;
            t->mix_vol = 1.0f;
            return t;
        }
    return NULL;
}
static Track *root_of(Track *t) { while (t->parent) t = t->parent; return t; }
static float interval(Track *t) { Track *r = root_of(t); return 4.0f / 3.0f * r->timebase * r->tempo / DAC; }
static int transpose_total(Track *t) { return t->transpose + (t->parent ? transpose_total(t->parent) : 0); }

static u8 rd8(Track *t) { return t->seq[t->pc++]; }
static u16 rd16(Track *t) { u16 v = be16(t->seq + t->pc); t->pc += 2; return v; }
static u32 rd24(Track *t) { u32 v = (u32)t->seq[t->pc] << 16 | (u32)t->seq[t->pc + 1] << 8 | t->seq[t->pc + 2]; t->pc += 3; return v; }
static u32 vlq(Track *t)
{
    u32 v = 0;
    for (int k = 0; k < 4; k++) {
        u8 b = rd8(t);
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    return 0;
}
static u32 reg_get(Track *t, int r)
{
    if (r < 14) return t->regs[r];
    if (r == 32) return (u32)t->regs[0] << 8 | t->regs[1];
    if (r == 33) return (u32)t->regs[4] << 16 | t->regs[5];
    if (r >= 0x40 && r < 0x50) return t->ports[r - 0x40];
    switch (r) {
    case 0x60: { u32 m = 0; for (int k = 0; k < 16; k++) if (t->child[k] && !t->child[k]->done) m |= 1u << k; return m; }
    case 0x62: return t->timebase;
    case 0x63: return (u16)t->transpose;
    case 0x64: return t->bend;
    case 0x65: return t->gate_rate;
    case 0x67: return t->bank;
    case 0x68: return t->prog;
    case 0x6D: return t->dr;
    }
    return 0;
}
static void reg_set(Track *t, int r, u32 v)
{
    v &= 0xFFFF;
    if (r < 14) t->regs[r] = r < 3 ? (v & 0xFF) : v;
    else if (r == 33) { t->regs[4] = 0; t->regs[5] = (u16)v; }
    else if (r >= 0x40 && r < 0x50) t->ports[r - 0x40] = (u16)v;
    else switch (r) {
        case 0x62: t->timebase = v; break;
        case 0x63: t->transpose = (s16)v; break;
        case 0x64: t->bend = v; break;
        case 0x65: t->gate_rate = v; break;
        case 0x67: t->bank = v; break;
        case 0x68: t->prog = v; break;
        case 0x6D: t->dr = (u16)v; break;
        }
    t->regs[3] = (u16)v;
}
static int cond(Track *t, int c)
{
    u16 f = t->regs[3];
    return c == 0 || (c == 1 && f == 0) || (c == 2 && f != 0) || (c == 3 && f == 1) || (c == 4 && f >= 0x8000) ||
           (c == 5 && f < 0x8000);
}
static void note_off(Track *t, int id, u16 dr)
{
    Chan *c = t->notes[id];
    t->notes[id] = NULL;
    if (c) chan_release(c, dr);
}
static void note_on(Track *t, int id, int key, int vel)
{
    key += transpose_total(t);
    note_off(t, id, 0);
    t->notes[id] = track_start(t, key, vel, 0);
}
/* JASSeqParser::execNoteOnGate + JASTrack::gateOn */
static void gate(Track *t, int key, int vel, u32 time, int flags)
{
    int f = (flags & 0x40 ? 2 : 0) | (flags & 0x80 ? 1 : 0) | (time == 0 ? 4 : 0);
    key += transpose_total(t);
    float st = time * (t->gate_rate / 100.0f);
    u32 dsp = st > 0 ? (u32)(st / interval(t)) : 0;
    u32 timer = (f & 6) ? 0 : dsp;
    int pitch = (f & 1) ? t->last_key : key;
    Chan *c;
    if (!t->ready) {
        note_off(t, 0, 0);
        t->notes[0] = track_start(t, pitch, vel, timer);
    } else if ((c = t->notes[0])) {
        if (!c->drum) c->key = (float)(pitch - c->w->base);
        c->vel = vel; c->timer = timer;
    }
    c = t->notes[0];
    if ((f & 1) && c) {
        int delta = key - pitch;
        if (dsp == 0) c->sweep = (float)delta;
        else { c->sweep = 0; c->sweep_to = (float)delta; }
        c->sweep_n = dsp;
    }
    t->last_key = key;
    t->ready = (f & 2) != 0;
    if (time) t->wait = time;
    else t->wait_note = 1;
}

/* opcode -> (numArgs, argWidths), JASSeqParser::sCmdInfo (0xA0..0xFF) */
static const u8 cmd_n[96] = {
    [0x11] = 3, [0x12] = 1, [0x13] = 4, [0x14] = 1, [0x18] = 2, [0x19] = 2, [0x1A] = 3, [0x1B] = 3,
    [0x21] = 2, [0x22] = 1, [0x23] = 1, [0x24] = 2, [0x26] = 1, [0x27] = 1, [0x28] = 2, [0x29] = 2, [0x2A] = 2,
    [0x2B] = 1, [0x30] = 2, [0x31] = 2, [0x32] = 1, [0x33] = 1, [0x34] = 2, [0x35] = 2, [0x36] = 2, [0x37] = 2,
    [0x38] = 2, [0x39] = 3, [0x3A] = 3, [0x3B] = 2, [0x3C] = 4, [0x40] = 1, [0x41] = 1, [0x42] = 1, [0x43] = 1,
    [0x47] = 2, [0x48] = 2, [0x49] = 5, [0x4A] = 2, [0x4B] = 1, [0x4C] = 4, [0x4D] = 1, [0x51] = 1, [0x53] = 1,
    [0x54] = 1, [0x55] = 1, [0x58] = 2, [0x59] = 1};
static const u16 cmd_w[96] = {
    [0x13] = 0x40, [0x19] = 4, [0x1A] = 0x10, [0x1B] = 0x14, [0x21] = 8, [0x23] = 2, [0x24] = 8, [0x27] = 2,
    [0x28] = 8, [0x29] = 0xB, [0x2A] = 0xB, [0x2B] = 1, [0x31] = 0xC, [0x34] = 0xC, [0x35] = 0xC, [0x38] = 4,
    [0x39] = 0x30, [0x3A] = 0x10, [0x3C] = 0xE0, [0x40] = 1, [0x41] = 1, [0x47] = 4, [0x48] = 8, [0x49] = 0x155,
    [0x4A] = 4, [0x4C] = 0x55, [0x4D] = 2, [0x53] = 2, [0x54] = 1, [0x55] = 1, [0x58] = 4, [0x59] = 1};

static void exec(Track *t, int op, const u32 *a);
static void step(Track *t)
{
    u8 op = rd8(t);
    if (op < 0x80) {
        u8 flags = rd8(t), vel = rd8(t);
        if ((flags & 7) == 0) gate(t, op, vel, vlq(t), flags);
        else note_on(t, flags & 7, op, vel);
        return;
    }
    if ((op & 0xF0) == 0x80) { if (op & 7) note_off(t, op & 7, 0); return; }
    u16 over = 0;
    if ((op & 0xF0) == 0x90) {
        int n = (op & 7) + 1;
        u8 mask = rd8(t);
        u16 bits = 3;
        for (int k = 0; k < n; k++) { if (mask & 0x80) over |= bits; mask <<= 1; bits <<= 2; }
        op = rd8(t);
    }
    if (op == 0xB0) { rd8(t); return; }
    if (op == 0xF0) { t->wait = vlq(t); return; }
    if (op == 0xFD) { /* printf: its string, then one byte per % */
        const u8 *s = t->seq + t->pc;
        int pc = 0;
        while (*s) { if (*s == '%') pc++; s++; }
        t->pc = (u32)(s - t->seq) + 1 + pc;
        return;
    }
    u32 a[5] = {0};
    int na = cmd_n[op - 0xA0];
    u16 rt = cmd_w[op - 0xA0] | over;
    for (int k = 0; k < na; k++, rt >>= 2) {
        switch (rt & 3) {
        case 0: a[k] = rd8(t); break;
        case 1: a[k] = rd16(t); break;
        case 2: a[k] = rd24(t); break;
        default: a[k] = reg_get(t, rd8(t)); break;
        }
    }
    exec(t, op, a);
}
static float param8(u32 v) /* the 8-bit parameter expansion of 0xB8 / 0xBA */
{
    s16 s = (s16)(v << 8);
    if (!(v & 0x80)) s |= (s16)((v << 1) & 0xFF);
    return s / 32767.0f;
}
static void exec(Track *t, int op, const u32 *a)
{
    switch (op) {
    case 0xB1: note_on(t, a[1] & 7, a[0], a[2]); break;
    case 0xB2: note_off(t, a[0] & 7, 0); break;
    case 0xB3: gate(t, a[1], a[2], a[3], a[0]); break;
    case 0xB4: t->last_key = a[0] + transpose_total(t); break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: {
        float v = (op == 0xB8 || op == 0xBA) ? param8(a[1]) : (s16)a[1] / 32767.0f;
        u32 n = (op == 0xBA || op == 0xBB) ? a[2] : 0;
        if (a[0] < 6) {
            if (n) { t->ramp_to[a[0]] = v; t->ramp_n[a[0]] = n; }
            else { t->params[a[0]] = v; t->ramp_n[a[0]] = 0; }
        }
        break;
    }
    case 0xC1: if (a[0] < 16) t->child[a[0]] = track_new(t->snd, t, a[1]); break;
    case 0xC2: if (a[0] < 16 && t->child[a[0]]) t->child[a[0]]->done = 1; break;
    case 0xC3: case 0xC4:
        if ((op == 0xC3 || cond(t, a[0])) && t->sp < 8) { t->loopn[t->sp] = 0xFFFFFFFF; t->stack[t->sp++] = t->pc; t->pc = op == 0xC3 ? a[0] : a[1]; }
        break;
    case 0xC5: case 0xC6:
        if ((op == 0xC5 || cond(t, a[0])) && t->sp > 0) t->pc = t->stack[--t->sp];
        break;
    case 0xC7: case 0xC8: if (op == 0xC7 || cond(t, a[0])) t->pc = op == 0xC7 ? a[0] : a[1]; break;
    case 0xC9: case 0xCA: {
        const u8 *e = t->seq + a[1] + 3 * a[0];
        u32 to = (u32)e[0] << 16 | (u32)e[1] << 8 | e[2];
        if (op == 0xCA && t->sp < 8) { t->loopn[t->sp] = 0xFFFFFFFF; t->stack[t->sp++] = t->pc; }
        t->pc = to;
        break;
    }
    case 0xCB: if (t->sp < 8) { t->loopn[t->sp] = a[0]; t->stack[t->sp++] = t->pc; } break;
    case 0xCC:
        if (t->sp > 0) {
            u32 n = t->loopn[t->sp - 1];
            if (n) n--;
            if (!n) t->sp--;
            else { t->loopn[t->sp - 1] = n; t->pc = t->stack[t->sp - 1]; }
        }
        break;
    case 0xD0: case 0xD6: case 0xD7: reg_set(t, a[1], 0); break;
    case 0xD2: case 0xD3: t->regs[3] = 0; break;
    case 0xD8: reg_set(t, a[0], a[1]); break;
    case 0xD9: case 0xDA: {
        u32 o = a[0], r = a[1], v = a[2], cur = reg_get(t, r);
        switch (o) {
        case 0: reg_set(t, r, v); break;
        case 1: reg_set(t, r, v + cur); break;
        case 2: reg_set(t, r, cur - v); break;
        case 3: t->regs[3] = (u16)(cur - v); break;
        case 4: reg_set(t, 33, v * cur); break;
        case 5: reg_set(t, r, v & cur); break;
        case 6: reg_set(t, r, v | cur); break;
        case 7: reg_set(t, r, v ^ cur); break;
        case 8: reg_set(t, r, v ? (u32)rand() % v : 0); break;
        case 9: reg_set(t, r, cur << v); break;
        case 10: reg_set(t, r, cur >> v); break;
        }
        break;
    }
    case 0xDB: reg_set(t, a[1], (u32)-(s32)reg_get(t, a[1])); break;
    case 0xDC: {
        u32 o = a[0], w = o == 12 ? 1 : o == 13 ? 2 : o == 14 ? 3 : 4, at = o == 16 ? a[2] + a[3] : a[2] + a[3] * w;
        u32 v = 0;
        for (u32 k = 0; k < w; k++) v = v << 8 | t->seq[at + k];
        reg_set(t, a[1], v);
        break;
    }
    case 0xE0: t->tempo = a[0]; break;
    case 0xE1: t->bank = a[0] >> 8; t->prog = a[0] & 0xFF; break;
    case 0xE2: t->bank = a[0]; break;
    case 0xE3: t->prog = a[0]; break;
    case 0xE7: if (a[0] < 2 && t->over_on[a[0]]) t->over[a[0]].scale = a[1] / 16383.0f; break;
    case 0xE8:
        if (a[0] < 2) {
            OscData *d = &t->over[a[0]];
            d->target = (u8)a[0]; d->rate = 1; d->scale = 1; d->vertex = 0;
            d->table = t->seq + a[1]; d->rel = NULL;
            t->over_on[a[0]] = 1;
        }
        break;
    case 0xE9: { /* SimpleADSR: {0, A, 0x7FFF} {0, D, 0x7FFF} {0, S, L} {0xE, 0, 0}, release as the direct release */
        static const u16 modes[4] = {0, 0, 0, 0xE};
        u16 tv[4] = {(u16)a[0], (u16)a[1], (u16)a[2], 0}, vv[4] = {0x7FFF, 0x7FFF, (u16)a[3], 0};
        for (int k = 0; k < 4; k++) {
            u8 *p = t->adsr + 6 * k;
            p[0] = modes[k] >> 8; p[1] = (u8)modes[k]; p[2] = tv[k] >> 8; p[3] = (u8)tv[k]; p[4] = vv[k] >> 8; p[5] = (u8)vv[k];
        }
        OscData *d = &t->over[0];
        d->target = 0; d->rate = 1; d->scale = 1; d->vertex = 0; d->table = t->adsr; d->rel = NULL;
        t->over_on[0] = 1;
        t->dr = (u16)a[4];
        break;
    }
    case 0xF1: t->wait = a[0]; break;
    case 0xF9: reg_set(t, 3, 0xFFFF); break;
    case 0xFF: t->done = 1; break;
    }
}

/* JASSeqCtrl::tickProc, then the children, then the timed parameters and JASTrack::updateTrack */
static void track_tick(Track *t)
{
    if (!t->done) {
        if (t->wait > 0) t->wait--;
        for (int guard = 0; guard < 256 && !t->done && t->wait == 0; guard++) {
            if (t->wait_note) {
                Chan *c = t->notes[0];
                if (c && c->alive) break;
                t->wait_note = 0;
            }
            step(t);
        }
    }
    for (int k = 0; k < 16; k++)
        if (t->child[k]) track_tick(t->child[k]);
    for (int k = 0; k < 6; k++)
        if (t->ramp_n[k]) { t->params[k] += (t->ramp_to[k] - t->params[k]) / t->ramp_n[k]; t->ramp_n[k]--; }
    float v = t->params[0] * t->params[0], o = t->params[1] * t->bend / 3.0f;
    if (t->parent) { t->mix_vol = t->parent->mix_vol * v; t->mix_oct = t->parent->mix_oct + o; }
    else { t->mix_vol = v * t->snd->vol; t->mix_oct = o; }
}
static int track_busy(Track *t) /* not done, or a child not done */
{
    if (!t->done) return 1;
    for (int k = 0; k < 16; k++)
        if (t->child[k] && track_busy(t->child[k])) return 1;
    return 0;
}
static void track_stop(Track *t) /* every track done, its notes released (the slots freed with the sound) */
{
    for (int k = 0; k < 16; k++)
        if (t->child[k]) track_stop(t->child[k]);
    for (int k = 0; k < 8; k++) note_off(t, k, 0);
    t->done = 1;
}

/* ---- sounds ---- */
static int map_code = -1; /* the player's ground sound code (se_map_code) */
void se_map_code(int code) { map_code = code; }

u32 se_start(u32 id, const float *pos)
{
    const SeRow *r = se_row(id);
    if (pos) { /* past place()'s silence (5000): no slot for it */
        float d[3] = {pos[0] - listener[0], pos[1] - listener[1], pos[2] - listener[2]};
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 5000.0f * 5000.0f) return 0;
    }
#ifdef SE_LOG
    printf("se%c %08x map %d\n", r ? '+' : '?', id, map_code); /* tests: started, or not in this sound.bin */
#endif
    if (!r) return 0;
    for (int i = 0; i < MAX_SND; i++)
        if (!so[i].used) {
            Sound *s = &so[i];
            memset(s, 0, sizeof *s);
            s->used = 1; s->id = id; s->handle = next_handle++; s->prio = r->prio; s->vol = s->base = r->vol / 255.0f; s->pitch = 1;
            s->acc = 1.0f;
            if (pos) { s->has_pos = 1; s->pos[0] = pos[0]; s->pos[1] = pos[1]; s->pos[2] = pos[2]; }
            s->root = track_new(s, NULL, r->seq);
            if (s->root && map_code > 0) /* AudSoundObject::setMapCodeToPort: the surface sounds read ports 11 / 12 */
                switch (id) { /* SE_PM_ HIPDROP, FALLDOWN_S / M, FOOTNOTE_L / R, SUB_L / R, JUMP, LAND, HAND, LAND_HEAVY */
                case 0x00020006u: case 0x00020013u: case 0x00020017u: case 0x00020025u: case 0x00020026u:
                case 0x00020027u: case 0x00020028u: case 0x00020029u: case 0x0002002Au: case 0x0002002Bu:
                case 0x00020038u:
                    s->root->ports[12] = (u16)((map_code - 1) / 7);
                    s->root->ports[11] = (u16)(((map_code - 1) % 7) * 12);
                }
            if (!s->root) {
#ifdef SE_LOG
                printf("se! %08x no track\n", id);
#endif
                s->used = 0; return 0;
            }
            return s->handle;
        }
#ifdef SE_LOG
    printf("se! %08x no slot\n", id);
#endif
    return 0;
}
void se_stop(u32 handle)
{
    for (int i = 0; i < MAX_SND; i++)
        if (so[i].used && so[i].handle == handle && !so[i].stopping) {
            so[i].stopping = 1;
            track_stop(so[i].root);
        }
}
u32 se_level(u32 id, const float *pos, u32 handle) /* JAUSoundObject::startLevelSound: alive while asked each frame */
{
    for (int i = 0; i < MAX_SND; i++)
        if (so[i].used && so[i].handle == handle && handle && !so[i].stopping) {
            so[i].level_seen = 2;
            if (pos) { so[i].pos[0] = pos[0]; so[i].pos[1] = pos[1]; so[i].pos[2] = pos[2]; }
            return handle;
        }
    u32 h = se_start(id, pos);
    for (int i = 0; i < MAX_SND && h; i++)
        if (so[i].handle == h) so[i].level_seen = 2;
    return h;
}
void se_port(u32 handle, int port, u16 value)
{
    for (int i = 0; i < MAX_SND; i++)
        if (so[i].used && so[i].handle == handle && so[i].root) so[i].root->ports[port & 15] = value;
}

void se_volpitch(u32 handle, float vol, float pitch) /* JAISound volume / pitch on a playing sound (set, not added up) */
{
    for (int i = 0; i < MAX_SND; i++)
        if (so[i].used && so[i].handle == handle) { so[i].vol = so[i].base * vol; so[i].pitch = pitch; }
}

/* JAUSoundAnimator::updateAnimSound over sound.bin's .bas tables: clip "<archive>/<clip>" went from frame `from`
 * (-1: just started) to `to`; an entry plays as the clip passes its note-on frame (round the loop too), at its
 * base volume (/ 127) and pitch; flag 0x40 holds it as a level sound from note-on to note-off. owner: the animation
 * (the level sounds' handles are kept per owner and entry). ponytail: the direction / once / stop flags, deltas and
 * repeat are not looked at */
static struct { const void *owner; u32 entry, handle; } anim_lv[32];
int se_anim(const char *clip, float from, float to, const float pos[3], const void *owner)
{
    int lo = 0, hi = (int)nbas - 1, at = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2, c = strcmp(bas[m].key, clip);
        if (!c) { at = m; break; }
        if (c < 0) lo = m + 1; else hi = m - 1;
    }
    if (at < 0) return 0;
    for (u32 k = 0; k < bas[at].n; k++) {
        const u8 *e = basent + bas[at].off + 0x20 * k;
        u32 id = be32(e), flags = be32(e + 0x10);
        float on = bef(e + 4), off = bef(e + 8), vol = e[0x14] / 127.0f, pitch = bef(e + 0x0C);
        if (pitch <= 0) pitch = 1;
        if (flags & 0x40) {
            if (to < on || to > off) continue;
            int slot = -1;
            for (int i = 0; i < 32; i++)
                if (anim_lv[i].owner == owner && anim_lv[i].entry == k + at * 64u) { slot = i; break; }
            for (int i = 0; i < 32 && slot < 0; i++)
                if (!anim_lv[i].owner) { slot = i; anim_lv[i].owner = owner; anim_lv[i].entry = k + at * 64u; anim_lv[i].handle = 0; }
            if (slot < 0) continue;
            u32 h0 = anim_lv[slot].handle;
            anim_lv[slot].handle = se_level(id, pos, h0);
            if (anim_lv[slot].handle != h0) se_volpitch(anim_lv[slot].handle, vol, pitch);
            continue;
        }
        int hit = from < 0 ? on <= to : (to >= from ? from < on && on <= to : on > from || on <= to);
        if (!hit) continue;
        u32 h = se_start(id, pos);
        if (h) se_volpitch(h, vol, pitch);
    }
    return 1;
}

void se_listener(const float eye[3], const float r[3])
{
    for (int k = 0; k < 3; k++) { listener[k] = eye[k]; right[k] = r[k]; }
}

/* the distance fall-off and pan of a positioned sound (ponytail: JAIAudience's curves not ported) */
static void place(const Sound *s, float *vol, float *pan)
{
    *vol = 1.0f; *pan = 0.5f;
    if (!s->has_pos) return;
    float d[3] = {s->pos[0] - listener[0], s->pos[1] - listener[1], s->pos[2] - listener[2]};
    float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > 500.0f) *vol = dist > 5000.0f ? 0.0f : 1.0f - (dist - 500.0f) / 4500.0f;
    if (dist > 1.0f) *pan = 0.5f + 0.5f * (d[0] * right[0] + d[1] * right[1] + d[2] * right[2]) / dist * 0.8f;
}

static void voice_off(int v)
{
    se_param(SD_VOICE(0, v) | SD_VPARAM_VOLL, 0);
    se_param(SD_VOICE(0, v) | SD_VPARAM_VOLR, 0);
    se_switch(0 | SD_SWITCH_KOFF, 1u << v);
    voice_of[v] = -1;
}

/* every voice off and flushed (a scene change: main.c clears this module's state next) */
void se_silence(void)
{
    for (int v = 0; v < NVOICE; v++) voice_off(v);
    se_flush();
    se_wait();
}

void se_update(int steps)
{
    if (!snd) return;
    for (int i = 0; i < 32; i++) /* animation level sounds that ended free their slot */
        if (anim_lv[i].owner) {
            int alive = 0;
            for (int k = 0; k < MAX_SND && !alive; k++) alive = so[k].used && so[k].handle == anim_lv[i].handle;
            if (!alive) anim_lv[i].owner = NULL;
        }
    /* level sounds not asked for this frame stop */
    for (int i = 0; i < MAX_SND; i++)
        if (so[i].used && so[i].level_seen && --so[i].level_seen == 0) se_stop(so[i].handle);
    sub_acc += SUBS_PER_FRAME * steps;
    while (sub_acc >= 1.0f) {
        sub_acc -= 1.0f;
        for (int i = 0; i < MAX_SND; i++) {
            Sound *s = &so[i];
            if (!s->used || s->stopping) continue;
            float iv = interval(s->root);
            while (s->acc >= 1.0f) { s->acc -= 1.0f; track_tick(s->root); }
            s->acc += iv;
        }
        for (int i = 0; i < MAX_CH; i++)
            if (ch[i].used && ch[i].alive) chan_sub(&ch[i]);
    }
    u32 endx = se_endx();
    /* voices: a dead channel's voice off; a one-shot voice at its end kills its channel */
    for (int v = 0; v < NVOICE; v++) {
        int c = voice_of[v];
        if (c < 0) continue;
        Chan *k = &ch[c];
        if (k->age >= 3 && (endx >> v & 1) && k->w->loop == 0xFFFFFFFF) k->alive = 0;
        if (!k->alive) { voice_off(v); k->voice = -1; }
    }
    for (int i = 0; i < MAX_CH; i++) {
        Chan *c = &ch[i];
        if (!c->used) continue;
        if (!c->alive) {
            if (c->voice < 0) {
                for (int k = 0; k < 8; k++) if (c->tr && c->tr->used && c->tr->notes[k] == c) c->tr->notes[k] = NULL;
                c->used = 0;
            }
            continue;
        }
        Track *t = c->tr;
        float vol = 1.0f, pitch = 1.0f;
        for (int k = 0; k < 2; k++) {
            if (!c->osc[k].d) continue;
            float x = osc_value(&c->osc[k]);
            if (c->osc[k].d->target == 0) vol *= x;
            else if (c->osc[k].d->target == 1) pitch *= x;
        }
        float dv, pan;
        place(t->snd, &dv, &pan);
        float amp = (c->vel / 127.0f) * (c->vel / 127.0f) * c->vol * vol * t->mix_vol * dv;
        if (amp < 0) amp = 0;
        float rate = c->w->rate * c->mul * pitch * t->snd->pitch * powf(2.0f, t->mix_oct + (c->key + c->sweep) / 12.0f);
        u32 p = (u32)(rate * 4096.0f / 48000.0f);
        if (p > 0x3FFF) p = 0x3FFF;
        float l = amp * sinf((1.0f - pan) * 1.5707963f), r = amp * sinf(pan * 1.5707963f);
        u16 vl = (u16)(l > 1.0f ? 0x3FFF : l * 0x3FFF), vr = (u16)(r > 1.0f ? 0x3FFF : r * 0x3FFF);
        if (c->voice < 0) {
            int v = -1;
            for (int k = 0; k < NVOICE && v < 0; k++) if (voice_of[k] < 0) v = k;
            if (v < 0) continue; /* ponytail: no stealing yet */
            c->voice = v; voice_of[v] = i; c->age = 0;
            se_param(SD_VOICE(0, v) | SD_VPARAM_ADSR1, 0x000F);
            se_param(SD_VOICE(0, v) | SD_VPARAM_ADSR2, 0x1FC0);
            se_addr(SD_VOICE(0, v) | SD_VADDR_SSA, SPU_BASE + c->w->spu);
            se_param(SD_VOICE(0, v) | SD_VPARAM_PITCH, (u16)p);
            se_param(SD_VOICE(0, v) | SD_VPARAM_VOLL, vl);
            se_param(SD_VOICE(0, v) | SD_VPARAM_VOLR, vr);
            se_switch(0 | SD_SWITCH_KON, 1u << v);
        } else {
            se_param(SD_VOICE(0, c->voice) | SD_VPARAM_PITCH, (u16)p);
            se_param(SD_VOICE(0, c->voice) | SD_VPARAM_VOLL, vl);
            se_param(SD_VOICE(0, c->voice) | SD_VPARAM_VOLR, vr);
        }
        c->age++;
    }
    /* a sound ends once its tracks are done and its channels gone */
    for (int i = 0; i < MAX_SND; i++) {
        Sound *s = &so[i];
        if (!s->used) continue;
        int busy = !s->stopping && track_busy(s->root);
        for (int k = 0; k < MAX_CH && !busy; k++) if (ch[k].used && ch[k].tr && ch[k].tr->snd == s) busy = 1;
        if (!busy) {
            for (int k = 0; k < MAX_TR; k++) if (tr[k].used && tr[k].snd == s) tr[k].used = 0;
            s->used = 0;
        }
    }
}

void se_stats(int *sounds, int *chans, int *voices) /* in use, for tests */
{
    *sounds = *chans = *voices = 0;
    for (int i = 0; i < MAX_SND; i++) *sounds += so[i].used;
    for (int i = 0; i < MAX_CH; i++) *chans += ch[i].used && ch[i].alive;
    for (int v = 0; v < NVOICE; v++) *voices += voice_of[v] >= 0;
}
