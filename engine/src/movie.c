#include "movie.h"

#include <audsrv.h>
#include <dma.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <kernel.h>
#include <libmpeg.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "game.h"
#include "gamedata.h"
#include "pad.h"
#include "render.h"
#include "lyt.h"
#include "menu.h"

/* MoviePlayingSequence.cpp sInfoTable: the open wipe (after the play starts) and the close (its frames before the
 * end, -1 at the end: a forced black), in 60 Hz steps (THP frames); a movie frame here is two. Kinds: 0 none, 1 black
 * forced, 2 black fade, 3 white (SystemWipeWhiteFade / WipeWhiteFade), -1 time: sFadeFrame (30) */
static const struct { const char *name; s8 open; s16 open_t; s8 close; s16 close_t; } info[] = {
    {"PrologueA", 2, 30, 1, -1}, {"PrologueB", 2, 30, 1, -1}, {"FinalBattle", 2, 30, 1, -1},
    {"EpilogueA", 3, 90, 3, 180}, {"EndingA", 3, 180, 1, -1}, {"EndingB", 2, 180, 1, 180},
};

static const MovieIo *io;
static u8 *buf[2], *ring;          /* the chunks read (one played, the next coming), the sound queued */
static u32 *sizes, nchunks, cur, vpos, vlen;
static int pending;                /* the chunk being read into buf[cur ^ 1], -1 none */
static u32 ring_cap, ring_rd, ring_n;
static u8 *pic;                    /* the decoded picture (RGBA32 in 16 x 16 macroblocks) */
static qword_t *upload;            /* its GIF chain to render_free_block */
static int pw, ph;

/* chunk n into the buffer not played (asynchronous on the disc); none past the last */
static void read_chunk(int n)
{
    pending = n < (int)nchunks ? n : -1;
    if (pending >= 0) io->start(buf[cur ^ 1], sizes[n]);
}

/* the chunk read becomes the one played: its sound queued, the following one asked */
static int chunk_take(void)
{
    if (pending < 0) return 0;
    while (!io->done()) {}
    dma_channel_wait(DMA_CHANNEL_toIPU, 0); /* the played buffer's video sent */
    cur ^= 1;
    u8 *c = buf[cur];
    SyncDCache(c, c + sizes[pending]);
    vlen = ((u32 *)c)[0];
    vpos = 0;
    for (u32 alen = ((u32 *)c)[1], i = 0; i < alen && ring_n < ring_cap;) {
        u32 wr = (ring_rd + ring_n) % ring_cap, n = ring_cap - wr;
        if (n > ring_cap - ring_n) n = ring_cap - ring_n;
        if (n > alen - i) n = alen - i;
        memcpy(ring + wr, c + 16 + vlen + i, n);
        ring_n += n; i += n;
    }
    read_chunk(pending + 1);
    return 1;
}

/* libmpeg's data: the playing chunk's video to the IPU, then the next chunk's */
static int data_cb(void *u)
{
    (void)u;
    if (vpos >= vlen && !chunk_take()) return 0;
    u32 n = vlen - vpos > (64u << 10) ? (64u << 10) : vlen - vpos;
    dma_channel_wait(DMA_CHANNEL_toIPU, 0);
    dma_channel_send_normal(DMA_CHANNEL_toIPU, buf[cur] + 16 + vpos, n >> 4, 0, 0);
    vpos += n;
    return 1;
}

/* the sequence header: the picture and its upload chain (each macroblock a TRXPOS and an image of 64 qwords) */
static void *init_cb(void *u, MPEGSequenceInfo *si)
{
    (void)u;
    if (pic && si->m_Width == pw && si->m_Height == ph) return pic;
    if (pic) return NULL; /* the size changed: not ours */
    pw = si->m_Width; ph = si->m_Height;
    render_reserve_blocks((ph + 31) / 32 * (640 / 64) * 32); /* its PSMCT32 pages (64 x 32, 32 blocks): the subtitles'
                                                              * textures after them */
    pic = memalign(64, pw * ph * 4);
    SyncDCache(pic, pic + pw * ph * 4);
    upload = memalign(64, (4 + 6 * (pw / 16) * (ph / 16) + 1) * 16);
    qword_t *q = upload;
    u32 tbp = render_free_block();
    DMATAG_CNT(q, 3, 0, 0, 0); q++;
    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TRXREG(16, 16), GS_REG_TRXREG); q++;
    PACK_GIFTAG(q, GS_SET_BITBLTBUF(0, 0, 0, tbp, 640 >> 6, GS_PSM_32), GS_REG_BITBLTBUF); q++;
    u8 *mb = pic;
    for (int y = 0; y < ph; y += 16)
        for (int x = 0; x < pw; x += 16, mb += 1024) {
            DMATAG_CNT(q, 4, 0, 0, 0); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
            PACK_GIFTAG(q, GS_SET_TRXPOS(0, 0, x, y, 0), GS_REG_TRXPOS); q++;
            PACK_GIFTAG(q, GS_SET_TRXDIR(0), GS_REG_TRXDIR); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(64, 1, 0, 0, 2, 0), 0); q++;
            DMATAG_REF(q, 64, (u32)mb, 0, 0, 0); q++;
        }
    DMATAG_END(q, 0, 0, 0, 0);
    FlushCache(0);
    return pic;
}

/* the queued sound into audsrv while it has room */
static void feed_audio(void)
{
    if (!audio_ready()) { ring_n = 0; return; }
    for (int room = audsrv_available() & ~3; room >= 1024 && ring_n;) {
        u32 n = ring_cap - ring_rd;
        if (n > ring_n) n = ring_n;
        if (n > (u32)room) n = room;
        if (n > 8192) n = 8192;
        audsrv_play_audio((const char *)ring + ring_rd, n);
        ring_rd = (ring_rd + n) % ring_cap;
        ring_n -= n;
        room -= n;
    }
}

#define GS_CSR ((volatile u64 *)0x12001000) /* bit 3 VSINT: a vblank since it was cleared (written 1) */

/* the next vblank, feeding the sound meanwhile */
static void vsync_wait(void)
{
    *GS_CSR = 8;
    while (!(*GS_CSR & 8)) feed_audio();
}

/* the wipe over frame f of n (MoviePlayingSequence exePlay / exeCloseWipeOnPlaying), 0xAABBGGRR */
static u32 wipe(int k, int f, int n)
{
    s32 a = 0, c = 0;
    s32 t = info[k].open_t, step = 2 * f;          /* 60 Hz steps since the open */
    if (step < t) { a = 0x80 - 0x80 * step / t; c = info[k].open == 3 ? 0xFF : 0; }
    if (info[k].close_t >= 0 && 2 * f >= 2 * n - info[k].close_t) { /* closeWipeFrame <= getMovieCurrentFrame */
        s32 s = 2 * f - (2 * n - info[k].close_t);
        a = info[k].close == 1 ? 0x80 : s >= 30 ? 0x80 : 0x80 * s / 30;
        c = info[k].close == 3 ? 0xFF : 0;
    }
    return (u32)a << 24 | (u32)c << 16 | (u32)c << 8 | (u32)c;
}

/* --- MovieSubtitles / MovieSubtitlesDataTable.cpp sSubtitlesInfo: per movie up to five {message, start, appear time}
 * (THP frames: 59.94 a second, two of each of ours). MoviePlayingSequence::exePlay's tryStartSubtitles appears the one
 * starting at the current frame; MovieSubtitles (CinemaSuper, formMessage mode 2: no typewriter, the text at once):
 * Talk -> TalkWait at once, at its step mAppearTime the next page (Talk again) or dead. The layout and the messages
 * are main.c's subs.bin (lyt.c / msg_get / the talk font loaded from it). */
static const struct { const char *movie; struct { const char *id; s16 start, time; } e[5]; } subs_info[] = {
    {"PrologueA", {{"Layout_StoryDemoKoopaTalk000", 3887, 132}, {"Layout_StoryDemoKoopaTalk001", 4020, 154},
                   {"Layout_StoryDemoKoopaTalk002", 4175, 120}, {"Layout_StoryDemoKoopaTalk003", 4680, 179},
                   {"Layout_StoryDemoKoopaTalk004", 4560, 120}}},
    {"PrologueB", {{"Layout_StoryDemoPeachTalk000", 4920, 120}, {"Layout_StoryDemoKameckTalk000", 5460, 180}}},
    {"FinalBattle", {{"Layout_StoryDemoRosettaTalk000", 2970, 159}, {"Layout_StoryDemoRosettaTalk006", 3130, 180}}},
    {"EpilogueA", {{"Layout_StoryDemoKoopaTalk005", 4607, 187}, {"Layout_StoryDemoKoopaTalk006", 4795, 226}}},
    {"EndingA", {{"Layout_StoryDemoRosettaTalk001", 4980, 119}, {"Layout_StoryDemoRosettaTalk002", 5100, 149},
                 {"Layout_StoryDemoRosettaTalk003", 5250, 120}}},
    {"EndingB", {{"Layout_StoryDemoRosettaTalk004", 1860, 134}, {"Layout_StoryDemoRosettaTalk005", 1995, 275}}},
};
enum { SUB_WAIT, SUB_TALK, SUB_TALKWAIT };
static struct { LytInst *l; const u16 *page; s16 start, time; int nrv, next, step; } sub[5];
static int nsub;
static const u8 *rum;   /* subs.bin's MRUM: {char movie[16]; s32 frame, pattern} (DemoPadRumbler) */
static const char *rum_movie;

static const u16 *next_page(const u16 *s) /* MR::getNextMessagePage */
{
    while (*s) {
        if (*s != 0x1A) { s++; continue; }
        int page = s[1] == 1 && s[2] == 1;
        s += 4 + (s[3] + 1) / 2;
        if (page) return *s == '\n' ? s + 1 : s;
    }
    return NULL;
}

static void subs_init(const char *name) /* MoviePlayingSequence's constructor: a MovieSubtitles per entry, dead */
{
    nsub = 0;
    for (u32 i = 0; i < sizeof subs_info / sizeof subs_info[0]; i++) {
        if (strcmp(subs_info[i].movie, name)) continue;
        for (int k = 0; k < 5 && subs_info[i].e[k].id; k++) { /* getSubtitlesMessageNum */
            const u16 *m = msg_get(subs_info[i].e[k].id);
            LytInst *l = m ? lyt_new("CinemaSuper") : NULL;
            if (!l) { printf("movie: no subtitle %s\n", subs_info[i].e[k].id); continue; }
            sub[nsub].l = l; sub[nsub].page = m;
            sub[nsub].start = subs_info[i].e[k].start; sub[nsub].time = subs_info[i].e[k].time;
            sub[nsub].nrv = SUB_WAIT; sub[nsub].next = -1;
            lyt_set_text_rec(l, "Text00", m); /* formMessage */
            nsub++;
        }
    }
}

static void sub_set(int i, int nrv) { sub[i].next = nrv; }

/* one 60 Hz step at THP frame t: tryStartSubtitles (the first starting there), DemoPadRumbler::update, then the
 * subtitles' nerves (MovementType_MovieSubtitles) */
static void subs_step(int t)
{
    for (int i = 0; i < nsub; i++)
        if (sub[i].start == t) { sub_set(i, SUB_TALK); break; } /* appear */
    for (u32 n = rum ? *(const u32 *)rum : 0, i = 0; i < n; i++) {
        const u8 *e = rum + 16 + 24 * i;
        if (!strncmp((const char *)e, rum_movie, 16) && *(const s32 *)(e + 16) == t) pad_rumble(*(const s32 *)(e + 20));
    }
    for (int i = 0; i < nsub; i++) {
        if (sub[i].next >= 0) { sub[i].nrv = sub[i].next; sub[i].next = -1; sub[i].step = 0; }
        if (sub[i].nrv == SUB_TALK) sub_set(i, SUB_TALKWAIT); /* updateTalking; isTextAppearedAll (mode 2) */
        else if (sub[i].nrv == SUB_TALKWAIT && sub[i].step == sub[i].time) {
            const u16 *n = next_page(sub[i].page);
            if (n) { sub[i].page = n; lyt_set_text_rec(sub[i].l, "Text00", n); sub_set(i, SUB_TALK); }
            else sub_set(i, SUB_WAIT); /* kill */
        }
        sub[i].step++;
        if (sub[i].next >= 0) { sub[i].nrv = sub[i].next; sub[i].next = -1; sub[i].step = 0; }
    }
}

static void subs_draw(void)
{
    for (int i = 0; i < nsub; i++)
        if (sub[i].nrv != SUB_WAIT) { lyt_update(sub[i].l); lyt_draw(sub[i].l); }
}

int movie_play(const char *name, const MovieIo *the_io, const u8 *rumble)
{
    char path[64];
    u32 size, head[512] __attribute__((aligned(64)));
    int k = 0;
    for (int i = 0; i < (int)(sizeof info / sizeof info[0]); i++)
        if (!strcmp(info[i].name, name)) k = i;
    io = the_io;
    snprintf(path, sizeof path, "movies/%s.smv", name);
    if (!io->open(path, &size)) { printf("movie: no %s\n", path); return 0; }
    io->start((u8 *)head, 2048);
    while (!io->done()) {}
    if (memcmp(head, "SMGV", 4)) { io->close(); return 0; }
    u32 frames = head[3];
    nchunks = head[4];
    sizes = memalign(64, nchunks * 4);
    memcpy(sizes, head + 6, nchunks * 4);
    buf[0] = memalign(64, head[5]);
    buf[1] = memalign(64, head[5]);
    ring_cap = 4 * 128128;                    /* four chunks' sound */
    ring = memalign(64, ring_cap);
    printf("movie: %s, %u frames, %u chunks (%u KB the biggest)\n", name, frames, nchunks, head[5] >> 10);
    if (!sizes || !buf[0] || !buf[1] || !ring) { printf("movie: out of memory\n"); io->close(); return 0; }
    ring_rd = ring_n = 0;
    cur = 1; vpos = vlen = 0;
    read_chunk(0);                           /* into buf[0]: the first chunk_take plays it and asks the next */
    if (audio_ready()) {
        struct audsrv_fmt_t fmt = {(int)head[1], 16, (int)head[2]};
        audsrv_set_format(&fmt);
        audsrv_set_volume(MAX_VOLUME);
    }
    rum = rumble; rum_movie = name;
    subs_init(name);
    int skippable = !strcmp(name, "FinalBattle") && gd_visited_any("KoopaBattleVs3Galaxy"); /* trySkip */

    dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
    s64 pts;
    pic = NULL; pw = ph = 0;
    MPEG_Initialize(data_cb, NULL, init_cb, NULL, &pts);
    int f = 0;
    u32 t0 = cycles();
    while (MPEG_Picture(pic, &pts)) {
        subs_step(2 * f); subs_step(2 * f + 1);
        pad_rumble_update(); /* the second 60 Hz step's (pad_update does one) */
        render_picture(upload, pw, ph, wipe(k, f, frames), nsub ? subs_draw : NULL);
        /* frame f's time (1001 / 30000 s of 294.912 MHz each, the Count wraps alike), then the next vblank: a slow
         * picture (an I one) is caught up by the quick ones after it, the sound kept with them */
        u32 at = t0 + (u32)((u64)(f + 1) * 9840230u);
        while ((s32)(cycles() - at) < 0) feed_audio();
        vsync_wait();
        render_show();
        feed_audio();
        f++;
        pad_update();
        if (skippable && pad_pressed(PAD_CROSS)) break;
    }
    MPEG_Destroy();
    if (audio_ready()) audsrv_stop_audio();
    while (!io->done()) {}
    io->close();
    free(upload); free(pic); free(ring); free(buf[1]); free(buf[0]); free(sizes);
    upload = NULL; pic = NULL;
    render_reserve_blocks(0);
    nsub = 0;
    printf("movie: %s ended at frame %d\n", name, f);
    return 1;
}
