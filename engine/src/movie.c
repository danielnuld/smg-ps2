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

/* MoviePlayingSequence.cpp sInfoTable: the open wipe (after the play starts) and the close (its frames before the
 * end, -1 at the end: a forced black), in 60 Hz steps (THP frames); a movie frame here is two. Kinds: 0 none, 1 black
 * forced, 2 black fade, 3 white (SystemWipeWhiteFade / WipeWhiteFade), -1 time: sFadeFrame (30) */
static const struct { const char *name; s8 open; s16 open_t; s8 close; s16 close_t; u8 luigi; } info[] = {
    {"PrologueA", 2, 30, 1, -1, 0}, {"PrologueB", 2, 30, 1, -1, 0}, {"FinalBattle", 2, 30, 1, -1, 1},
    {"EpilogueA", 3, 90, 3, 180, 1}, {"EndingA", 3, 180, 1, -1, 1}, {"EndingB", 2, 180, 1, 180, 0},
}; /* luigi: mMovieNameLuigi ("<name>Luigi") */

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

int movie_play(const char *name, const MovieIo *the_io)
{
    char path[64];
    u32 size, head[512] __attribute__((aligned(64)));
    int k = 0;
    for (int i = 0; i < (int)(sizeof info / sizeof info[0]); i++)
        if (!strcmp(info[i].name, name)) k = i;
    io = the_io;
    snprintf(path, sizeof path, "movies/%s%s.smv", name, info[k].luigi && gd_player_luigi() ? "Luigi" : ""); /* getMovieName */
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
    int skippable = !strcmp(name, "FinalBattle") && gd_visited_any("KoopaBattleVs3Galaxy"); /* trySkip */

    dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
    s64 pts;
    pic = NULL; pw = ph = 0;
    MPEG_Initialize(data_cb, NULL, init_cb, NULL, &pts);
    int f = 0;
    u32 t0 = cycles();
    while (MPEG_Picture(pic, &pts)) {
        render_picture(upload, pw, ph, wipe(k, f, frames));
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
    printf("movie: %s ended at frame %d\n", name, f);
    return 1;
}
