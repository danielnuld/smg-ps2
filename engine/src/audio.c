#include "audio.h"

#include "me.h"

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
/* fopen; dev builds with XF=-DPACKS_OVERLAY=dir try host:dir/<path past host:> first (main.c's packs do the same),
 * then host:packs/<path past host:> (the release set tools/disc.py packs: out/packs/music has the .rhy and the newer
 * streams, out/music neither), then the path itself */
static FILE *m_open(const char *path)
{
    char full[160];
    FILE *f;
    if (strncmp(path, "host:", 5)) return fopen(path, "rb");
#ifdef PACKS_OVERLAY
#define OVL_STR2(x) #x
#define OVL_STR(x) OVL_STR2(x)
    snprintf(full, sizeof full, "host:" OVL_STR(PACKS_OVERLAY) "/%s", path + 5);
    if ((f = fopen(full, "rb"))) return f;
#endif
    snprintf(full, sizeof full, "host:packs/%s", path + 5);
    if ((f = fopen(full, "rb"))) return f;
    return fopen(path, "rb");
}
#endif
static unsigned int music_loop;    /* byte offset of the loop start in the samples */
static unsigned int music_rate;
static int ready;
static int fade_t, fade_n;         /* audio_fade: frames done / total (0: none) */
static int fin_t, fin_n;           /* audio_seek's fade in */
/* AudBgmVolumeController's volume (MR::moveVolumeStageBGM / SubBGM: our one stream): to mv_to over mv_n frames */
static float mv_cur = 1.0f, mv_from = 1.0f, mv_to = 1.0f;
static int mv_t, mv_n;
void audio_move_volume(float v, int frames)
{
    mv_from = mv_cur; mv_to = v; mv_t = 0; mv_n = frames > 0 ? frames : 0;
    if (!mv_n) mv_cur = v;
}
static unsigned char chunk[8192] __attribute__((aligned(64)));
static unsigned char rhy[24 << 10] __attribute__((aligned(64))); /* the stream's rhythm (tools/rhythm.py .rhy) */

/* The music's .rhy beside it (a state file <track>@<k> shares its track's) for the ME sequencer: none, no rhythm */
static void rhythm_load(const char *path)
{
    char p[128];
    int n = snprintf(p, sizeof p, "%s", path), got = -1;
    char *dot = strrchr(p, '.'), *slash = strrchr(p, '/'), *at = strrchr(p, '@');
    if (n <= 0 || n + 4 >= (int)sizeof p || !dot) { me_rhythm(NULL, 0); return; }
    *(at && (!slash || at > slash) ? at : dot) = 0;
    strcat(p, ".rhy");
#ifdef DISC
    u32 lsn, size, lsn0 = m_lsn;
    if (disc_file(p, &lsn, &size) && size <= sizeof rhy) {
        m_lsn = lsn; /* m_read reads at m_lsn */
        got = 0;
        for (u32 o = 0; o < size && got >= 0;) {
            u32 k = size - o > 16384 ? 16384 : size - o;
            int tries = 0;
            while (!m_read(o, rhy + o, k) && ++tries < 1000000) {}
            if (tries >= 1000000) got = -1;
            else { o += k; got = (int)o; }
        }
        m_lsn = lsn0;
    }
#else
    FILE *f = m_open(p);
    if (f) { got = (int)fread(rhy, 1, sizeof rhy, f); fclose(f); }
#endif
    me_rhythm(got > 0 ? rhy : NULL, got > 0 ? (u32)got : 0);
}

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
    music = m_open(path);
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
    audsrv_set_volume((int)(MAX_VOLUME * mv_cur));
    fade_n = fin_n = 0;
    printf("audio: %s, %u Hz, loop at byte %u\n", path, hdr[1], music_loop);
    rhythm_load(path);
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
    me_rhythm(NULL, 0); /* AudBgmRhythmStrategy::reject */
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
    int moved = mv_n > 0;
    if (mv_n) { mv_cur = mv_from + (mv_to - mv_from) * ++mv_t / mv_n; if (mv_t >= mv_n) mv_n = 0; }
    if (fade_n) { /* a linear fade, then the stream closed */
        if (++fade_t >= fade_n) { audio_stop(); return; }
        audsrv_set_volume((int)(MAX_VOLUME * mv_cur) * (fade_n - fade_t) / fade_n);
    } else if (fin_n) {
        if (++fin_t >= fin_n) fin_n = 0;
        audsrv_set_volume((int)(MAX_VOLUME * mv_cur) * (fin_n ? fin_t : 1) / (fin_n ? fin_n : 1));
    } else if (moved) audsrv_set_volume((int)(MAX_VOLUME * mv_cur));
    audio_feed();
    if (music) me_rhythm_tick(audio_pos(), music_loop);
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
