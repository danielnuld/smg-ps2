#include "audio.h"

#include <audsrv.h>
#include <kernel.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

/* IOP modules linked into the ELF (assets.S, copied from the SDK by build.sh) */
extern const unsigned char libsd_irx[], libsd_irx_end[], audsrv_irx[], audsrv_irx_end[];

#ifdef DISC
/* The disc build streams the BGMs from PACKS.BIN (music/<name>.pcm, tools/disc.py): a 128 KB ring (a second at
 * 32 kHz stereo) refilled 32 KB at a time by reads started while the drive is idle and taken a later frame (main.c
 * disc_read_async: no frame waits for the drive, and a zone streaming in leaves the second in the ring to bridge
 * it), fed to audsrv as on the host. "music" is then only a flag. */
typedef struct { int dummy; } MusicFile;
static MusicFile music_disc;
static MusicFile *music;
static u32 m_lsn, m_size, m_off;   /* the file's first sector, its bytes, the next byte to read */
#define RING (128 << 10)
#define CH (32 << 10)              /* a read's bytes (whole sectors, the ring's room checked for them) */
static unsigned char ring[RING] __attribute__((aligned(64)));
static unsigned char sect[CH + 2048] __attribute__((aligned(64)));
static u32 r_head, r_fill;         /* ring: next byte out, bytes in */
static u32 m_seq, m_skip, m_n;     /* the read in flight (0 none): its sequence, the bytes to drop, the bytes to keep */
int disc_file(const char *path, u32 *lsn, u32 *size);
int disc_read_idle(u32 lsn, u32 sectors, void *dst);
u32 disc_read_async(u32 lsn, u32 sectors, void *dst);
int disc_read_done(u32 seq);
static int m_read(u32 off, void *dst, u32 n) /* n bytes at off (at most 14 KB), synchronously; 0: drive busy */
{
    u32 sec = off / 2048, skip = off % 2048, ns = (skip + n + 2047) / 2048;
    if (!disc_read_idle(m_lsn + sec, ns, sect)) return 0;
    memcpy(dst, sect + skip, n);
    return 1;
}
#else
static FILE *music;
#endif
static unsigned int music_loop;    /* byte offset of the loop start in the samples */
static unsigned int music_rate;
static int ready;
static int fade_t, fade_n;         /* audio_fade: frames done / total (0: none) */
static int fin_t, fin_n;           /* audio_seek's fade in */
static unsigned char chunk[8192] __attribute__((aligned(64)));

int audio_init(void)
{
    int ret;
    if (SifExecModuleBuffer((void *)libsd_irx, libsd_irx_end - libsd_irx, 0, NULL, &ret) < 0 ||
        SifExecModuleBuffer((void *)audsrv_irx, audsrv_irx_end - audsrv_irx, 0, NULL, &ret) < 0 || audsrv_init() != 0) {
        printf("audio: audsrv not started\n");
        return -1;
    }
    ready = 1;
    return 0;
}

int audio_music(const char *path)
{
    unsigned int hdr[4];
    if (!ready) return -1;
#ifdef DISC
    music = NULL;
    if (strncmp(path, "host:", 5) == 0) path += 5; /* game.c names the host file: the disc has music/<name>.pcm */
    int tries = 0;
    if (!disc_file(path, &m_lsn, &m_size)) { printf("audio: no music %s\n", path); return -1; }
    while (!m_read(0, hdr, 16) && ++tries < 1000000) {} /* the drive may be finishing a pack read */
    if (memcmp(hdr, "SMGM", 4) != 0) { printf("audio: no music %s\n", path); return -1; }
    music = &music_disc;
    m_off = 16;
    r_head = r_fill = 0;
    m_seq = 0;
    { static int once; if (!once++) printf("audio: audsrv ring %d bytes\n", audsrv_available()); }
#else
    if (music) fclose(music);
    music = fopen(path, "rb");
    if (!music || fread(hdr, 4, 4, music) != 4 || memcmp(hdr, "SMGM", 4) != 0) {
        printf("audio: no music %s\n", path);
        if (music) fclose(music);
        music = NULL;
        return -1;
    }
#endif
    struct audsrv_fmt_t fmt = {(int)hdr[1], 16, (int)hdr[2]};
    music_loop = hdr[3];
    music_rate = hdr[1];
    audsrv_set_format(&fmt);
    audsrv_set_volume(MAX_VOLUME);
    fade_n = fin_n = 0;
    printf("audio: %s, %u Hz, loop at byte %u\n", path, hdr[1], music_loop);
    return 0;
}

int audio_ready(void) { return ready; }
int audio_playing(void) { return music != NULL; } /* a stream playing (a one-shot ends by itself) */

void audio_stop(void)
{
#ifndef DISC
    if (music) fclose(music);
#endif
    music = NULL;
    fade_n = 0;
}

unsigned audio_pos(void)
{
    if (!music) return 0;
#ifdef DISC
    return m_off - 16 - r_fill;
#else
    return (u32)ftell(music) - 16;
#endif
}

void audio_seek(unsigned pos, int ahead, int fade_in)
{
    if (!music) return;
    pos += (u32)ahead * (music_rate * 4 / 60);
#ifdef DISC
    u32 size = m_size - 16;
#else
    fseek(music, 0, SEEK_END);
    u32 size = (u32)ftell(music) - 16;
#endif
    if (pos >= size) pos = music_loop == 0xFFFFFFFFu || music_loop >= size ? size : music_loop + (pos - music_loop) % (size - music_loop);
    pos &= ~3u;
#ifdef DISC
    m_off = 16 + pos;
    r_head = r_fill = 0;
    while (m_seq && !disc_read_done(m_seq)) {} /* the read in flight (for the old place) dropped */
    m_seq = 0;
#else
    fseek(music, 16 + pos, SEEK_SET);
#endif
    fin_n = fade_in > 0 ? fade_in : 0; fin_t = 0;
    if (fin_n) audsrv_set_volume(0);
}

void audio_fade(int frames)
{
    if (!music) return;
    if (frames <= 0) { audio_stop(); return; }
    fade_n = frames; fade_t = 0;
}

void audio_update(void)
{
    if (!music) return;
    if (fade_n) { /* a linear fade, then the stream closed */
        if (++fade_t >= fade_n) { audio_stop(); return; }
        audsrv_set_volume(MAX_VOLUME * (fade_n - fade_t) / fade_n);
    } else if (fin_n) {
        if (++fin_t >= fin_n) fin_n = 0;
        audsrv_set_volume(fin_n ? MAX_VOLUME * fin_t / fin_n : MAX_VOLUME);
    }
    audio_feed();
}

void audio_feed(void)
{
    if (!music) return;
#ifdef DISC
    /* the read in: into the ring (4-byte frames: offsets stay multiples of 4); the next started if there is room */
    if (m_seq && disc_read_done(m_seq)) {
        InvalidDCache(sect, sect + sizeof sect - 1);
        u32 tail = (r_head + r_fill) % RING, first = m_n < RING - tail ? m_n : RING - tail;
        memcpy(ring + tail, sect + m_skip, first);
        memcpy(ring, sect + m_skip + first, m_n - first);
        r_fill += m_n;
        m_seq = 0;
    }
    if (!m_seq && RING - r_fill >= CH) {
        u32 n = CH - 2048; /* the file's bytes: one more sector covers an offset not on a sector */
        if (n > m_size - m_off) n = m_size - m_off;
        n &= ~3u;
        u32 sec = m_off / 2048, skip = m_off % 2048, ns = (skip + n + 2047) / 2048;
        if (n && (m_seq = disc_read_async(m_lsn + sec, ns, sect))) {
            m_skip = skip; m_n = n;
            m_off += n;
            if (m_off >= m_size && music_loop != 0xFFFFFFFFu) m_off = 16 + music_loop; /* the end: back to the loop start (a one-shot: none) */
        }
    }
    if (music_loop == 0xFFFFFFFFu && m_off >= m_size && !m_seq && r_fill < 4) { audio_stop(); return; } /* a one-shot played out */
    int room = audsrv_available();
    while (room >= 1024 && r_fill >= 4) {
        u32 n = (u32)room < r_fill ? (u32)room : r_fill;
        if (n > RING - r_head) n = RING - r_head;
        if (n > sizeof chunk) n = sizeof chunk;
        n &= ~3u;
        memcpy(chunk, ring + r_head, n);
        audsrv_play_audio((const char *)chunk, n);
        { static u32 fed; if (fed < (1u << 20) && (fed += n) >= (1u << 20)) printf("audio: 1 MB of music streamed from the disc\n"); }
        r_head = (r_head + n) % RING;
        r_fill -= n;
        room -= n;
    }
#else
    int room = audsrv_available();
    while (room >= 1024) { /* whole frames of 4 bytes, at most a chunk per call */
        int want = room < (int)sizeof chunk ? room & ~3 : (int)sizeof chunk;
        int got = fread(chunk, 1, want, music);
        if (got > 0) {
            audsrv_play_audio((const char *)chunk, got & ~3);
            room -= got & ~3;
        }
        if (got < want) { /* the end: back to the loop start (the 16-byte header first); a one-shot stops */
            if (music_loop == 0xFFFFFFFFu) { audio_stop(); return; }
            fseek(music, 16 + music_loop, SEEK_SET);
        }
    }
#endif
}
