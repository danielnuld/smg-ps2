#include <kernel.h>
#ifdef DISC
#include <libcdvd.h>
#endif
#include <malloc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

#include "anim.h"
#include "audio.h"
#include "blob.h"
#include "camera.h"
#include "game.h"
#if defined(BENCH) && defined(DISC) /* console runs: udptty loses bursts, so the bench summary is paced (4 ms a line) */
static int print_pace_on;
static void print_pace(void) { if (print_pace_on) { unsigned c0 = cycles(); while (cycles() - c0 < 294912u * 4) {} } }
#define printf(...) (printf(__VA_ARGS__), print_pace())
#endif
#include "mem.h"
#include "pad.h"
#include "render.h"
#include "se.h"
#include "me.h"
#include "vmath.h"

#include "fx.h"
#include "lyt.h"
#include "menu.h"
#include "gamedata.h"
#include "mc.h"
#include "movie.h"
#include "sequence.h"
#include "packs_cfg.h" /* written by build.sh: PACKS = "<Galaxy>/<scenario>", or empty */

extern const u8 slice_blob[];

static Scene scene;

#ifdef PACKS
/* Streaming layout (tools/slice.py packs, task 5.1). At boot the base packs; the stage packs come with the first zone
 * swap (stream_update). A pack is a path under packs/ ("<Galaxy>/<n>/<file>", "resident.bin"):
 *   host build: host:packs/<path> (PCSX2 host filesystem, run the ELF from out/);
 *   DISC build: sectors of \PACKS.BIN;1 read with libcdvd, found through \PACKS.TOC;1 (tools/disc.py: u32 n, n x
 *   {char path[88], u32 offset, u32 size}, every pack on a sector of its own); only this scenario's entries and
 *   resident.bin are kept. Buffers are rounded up to whole sectors (PACK_ALLOC). */
static u32 pack_bytes;
/* the scenario played: PACKS (build.sh), or for host builds the first line of host:boot.txt when there is one
 * (tools/coverage.ps1 plays every scenario with one ELF that way) */
static char scen[64] = PACKS;

static void pick_scenario(void)
{
#ifndef DISC
    FILE *f = fopen("host:boot.txt", "rb");
    if (!f) return;
    int n = fread(scen, 1, sizeof scen - 1, f);
    fclose(f);
    scen[n > 0 ? n : 0] = 0;
    for (int i = 0; i < n; i++)
        if (scen[i] == '\r' || scen[i] == '\n') scen[i] = 0;
    if (!scen[0]) strcpy(scen, PACKS);
#endif
}
typedef struct { u32 base, size, pos; FILE *f; } Pack; /* DISC: base = first sector, f unused but non-NULL while open */

/* The packs' buffers come from one region taken at boot (first fit, neighbours merged when freed): the libraries'
 * small heap blocks (stdio FILEs, the music's buffer) can not split it, so after a scene change it is whole again
 * and the next scene's big stage packs fit as at boot */
#define PK_BLOCKS 160
static u8 *pk_base;
static u32 pk_size;
static struct { u32 off, size; u8 used; } pk_blk[PK_BLOCKS];
static int pk_n;

static void pk_init(void)
{
    for (pk_size = 24u << 20; pk_size >= (4u << 20); pk_size -= 256u << 10) /* all the heap has but ~1.5 MB */
        if ((pk_base = memalign(64, pk_size + (1536u << 10)))) break;
    free(pk_base);
    pk_base = memalign(64, pk_size);
    pk_blk[0].off = 0; pk_blk[0].size = pk_size; pk_blk[0].used = 0;
    pk_n = 1;
    printf("packs: %u KB region\n", pk_size >> 10);
}

static void *pk_alloc(u32 n);
static void pk_free(const void *p);

/* a movie (movie.c, between two scenes: no pack loaded) takes its memory and libmpeg's from the packs' region:
 * memalign / free wrapped (Makefile), so the heap above it stays as it was and the region whole after */
static int pk_movie;
void *__real_memalign(size_t align, size_t n);
void __real_free(void *p);
void *__wrap_memalign(size_t align, size_t n) { return pk_movie && align <= 64 ? pk_alloc(n) : __real_memalign(align, n); }
void __wrap_free(void *p)
{
    if (pk_movie && (u8 *)p >= pk_base && (u8 *)p < pk_base + pk_size) pk_free(p);
    else __real_free(p);
}

static void *pk_alloc(u32 n)
{
    n = (n + 63) & ~63u;
    for (int i = 0; i < pk_n; i++) {
        if (pk_blk[i].used || pk_blk[i].size < n) continue;
        if (pk_blk[i].size > n && pk_n < PK_BLOCKS) { /* the rest stays free after it */
            memmove(&pk_blk[i + 2], &pk_blk[i + 1], (pk_n - i - 1) * sizeof pk_blk[0]);
            pk_blk[i + 1].off = pk_blk[i].off + n; pk_blk[i + 1].size = pk_blk[i].size - n; pk_blk[i + 1].used = 0;
            pk_blk[i].size = n;
            pk_n++;
        }
        pk_blk[i].used = 1;
        return pk_base + pk_blk[i].off;
    }
    return NULL;
}

/* a used block cut to its first n bytes: the rest freed (merged with a free block after it) */
static void pk_shrink(const void *p, u32 n)
{
    n = (n + 63) & ~63u;
    for (int i = 0; i < pk_n; i++) {
        if (pk_base + pk_blk[i].off != p) continue;
        if (pk_blk[i].size <= n) return;
        u32 cut = pk_blk[i].size - n;
        pk_blk[i].size = n;
        if (i + 1 < pk_n && !pk_blk[i + 1].used) { pk_blk[i + 1].off -= cut; pk_blk[i + 1].size += cut; }
        else if (pk_n < PK_BLOCKS) {
            memmove(&pk_blk[i + 2], &pk_blk[i + 1], (pk_n - i - 1) * sizeof pk_blk[0]);
            pk_blk[i + 1].off = pk_blk[i].off + n; pk_blk[i + 1].size = cut; pk_blk[i + 1].used = 0;
            pk_n++;
        } else pk_blk[i].size += cut; /* no block left: kept whole */
        return;
    }
}

static void pk_free(const void *p)
{
    if (!p) return;
    for (int i = 0; i < pk_n; i++) {
        if (pk_base + pk_blk[i].off != p) continue;
        pk_blk[i].used = 0;
        if (i + 1 < pk_n && !pk_blk[i + 1].used) { /* merge the next */
            pk_blk[i].size += pk_blk[i + 1].size;
            memmove(&pk_blk[i + 1], &pk_blk[i + 2], (pk_n - i - 2) * sizeof pk_blk[0]);
            pk_n--;
        }
        if (i > 0 && !pk_blk[i - 1].used) { /* into the previous */
            pk_blk[i - 1].size += pk_blk[i].size;
            memmove(&pk_blk[i], &pk_blk[i + 1], (pk_n - i - 1) * sizeof pk_blk[0]);
            pk_n--;
        }
        return;
    }
}

#ifdef DISC
#define SECTOR 2048
#define PACK_ALLOC(n) (((n) + SECTOR - 1) & ~(SECTOR - 1))
typedef struct { char path[88]; u32 off, size; } TocEntry;
static const TocEntry *toc[512]; /* into toc_all: a scenario's packs, the shared ones, the movies, the music (~230 with .rhy) */
static int ntoc;
static u8 *toc_all; /* the whole PACKS.TOC, kept (toc_select picks a scene's entries) */
static u32 toc_n;
static u32 bin_lsn;
static sceCdRMode cd_mode = {5, SCECdSpinNom, SCECdSecS2048, 0};

/* whole sectors from `lsn` into `dst` (64-aligned, PACK_ALLOC long); wait: block until they are in */
static u32 cd_seq; /* reads started (disc_read_async: a later read means an earlier one is in) */
static int cd_start(u32 lsn, u32 sectors, void *dst) /* a read begun; one refused (drive not ready, a command still
                                                        * held) is asked again, logged; 0: never taken */
{
    for (int tries = 0;; tries++) {
        sceCdSync(0); /* one read at a time: audio.c's may still be going */
        cd_seq++;
        if (sceCdRead(lsn, sectors, dst, &cd_mode)) return 1;
        printf("cd: read %u + %u refused, again (%d)\n", lsn, sectors, tries);
        if (tries == 300) return 0;
    }
}
static int cd_failed(u32 lsn, u32 sectors) /* the read just finished: an error (logged), read again twice at most; then
                                              * taken as it came (Neutrino's sceCdGetError may stay set on good data:
                                              * "packs: cannot read subs.bin" on the console) */
{
    static u32 err_lsn, errs;
    int e = sceCdGetError();
    if (e == SCECdErNO) return 0;
    errs = lsn == err_lsn ? errs + 1 : 1;
    err_lsn = lsn;
    printf("cd: read %u + %u error %d%s\n", lsn, sectors, e, errs > 2 ? ", taken" : ", again");
    return errs <= 2;
}
static int cd_read(u32 lsn, u32 sectors, void *dst, int wait)
{
    for (int tries = 0; tries < 30; tries++) {
        if (!cd_start(lsn, sectors, dst)) return 0;
        if (!wait) return 1;
        sceCdSync(0);
        if (cd_failed(lsn, sectors)) continue;
        InvalidDCache(dst, (u8 *)dst + sectors * SECTOR - 1);
        return 1;
    }
    return 0;
}

/* Boot menu: every scenario on the disc (the TOC's "<Galaxy>/<n>/scene.bin" entries), D-pad up / down (left / right
 * a page), X or START plays it; the build's PACKS is the first choice, played after 10 s without input. Sets scen. */
#define MENU_MAX 192
#define MENU_ROWS 12
static void disc_menu(const u8 *entries, u32 n)
{
    static char names[MENU_MAX][40];
    int count = 0, sel = 0;
    for (u32 i = 0; i < n && count < MENU_MAX; i++) {
        const char *p = ((const TocEntry *)(entries + i * sizeof(TocEntry)))->path;
        const char *tail = strstr(p, "/scene.bin");
        if (!tail || tail[10] || tail - p >= 40) continue;
        memcpy(names[count], p, tail - p);
        names[count][tail - p] = 0;
        if (strcmp(names[count], scen) == 0) sel = count;
        count++;
    }
    if (!count) return;
    { /* the release boots straight into the build's scenario: the menu only with SELECT held at boot */
        int sel_held = 0;
        for (int i = 0; i < 4; i++) { pad_update(); sel_held |= pad_held(PAD_SELECT); }
        if (!sel_held) return;
    }
    static const float eye[3] = {0, 0, 1}, at[3] = {0, 0, 0}, up[3] = {0, 1, 0};
    for (int f = 0, touched = 0;; f++) {
        pad_update();
        touched |= pad_held(PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
        if (!touched && f >= 600) break;
        if (pad_pressed(PAD_UP)) sel = (sel + count - 1) % count;
        if (pad_pressed(PAD_DOWN)) sel = (sel + 1) % count;
        if (pad_pressed(PAD_LEFT)) sel = sel >= MENU_ROWS ? sel - MENU_ROWS : 0;
        if (pad_pressed(PAD_RIGHT)) sel = sel + MENU_ROWS < count ? sel + MENU_ROWS : count - 1;
        if (f > 10 && (pad_pressed(PAD_CROSS) || pad_pressed(PAD_START))) break;
        render_begin(eye, at, up);
        char line[64];
        snprintf(line, sizeof line, "SCENARIO %d / %d   X: PLAY", sel + 1, count);
        render_text(16, 12, line);
        int first = sel - MENU_ROWS / 2 < 0 ? 0 : sel - MENU_ROWS / 2;
        if (first + MENU_ROWS > count) first = count > MENU_ROWS ? count - MENU_ROWS : 0;
        for (int i = first; i < count && i < first + MENU_ROWS; i++) {
            snprintf(line, sizeof line, "%s %s", i == sel ? ">" : " ", names[i]);
            render_text(16, 36 + 14 * (i - first), line);
        }
        render_end();
    }
    strcpy(scen, names[sel]);
}

static void toc_load(void)
{
    sceCdlFILE ft, fb;
    if (toc_all) return;
    sceCdInit(SCECdINoD);
    sceCdMmode(SCECdMmodeDvd);
    if (!sceCdSearchFile(&ft, "\\PACKS.TOC;1") || !sceCdSearchFile(&fb, "\\PACKS.BIN;1")) {
        render_fatal("packs: no PACKS.TOC / PACKS.BIN on the disc\n");
    }
    bin_lsn = fb.lsn;
    u8 *t = memalign(64, PACK_ALLOC(ft.size));
    if (!t || !cd_read(ft.lsn, PACK_ALLOC(ft.size) / SECTOR, t, 1)) {
        render_fatal("packs: cannot read PACKS.TOC\n");
    }
    toc_all = t;
    toc_n = *(const u32 *)t;
    disc_menu(t + 4, toc_n);
}

/* this scenario's entries and the shared packs from the whole TOC (each scene) */
static void toc_select(void)
{
    size_t lp = strlen(scen);
    ntoc = 0;
    for (u32 i = 0; i < toc_n; i++) {
        const TocEntry *e = (const TocEntry *)(toc_all + 4 + i * sizeof(TocEntry));
        if ((strncmp(e->path, scen, lp) == 0 && e->path[lp] == '/') || strcmp(e->path, "resident.bin") == 0 || strcmp(e->path, "sound.bin") == 0 ||
            strcmp(e->path, "resident_luigi.bin") == 0 || strcmp(e->path, "letter.bin") == 0 ||
            strcmp(e->path, "subs.bin") == 0 || /* the movies' subtitles (movie.c; missing on the disc until 2026-10-24) */
            strcmp(e->path, "menus.bin") == 0 || strcmp(e->path, "menus3d.bin") == 0 ||
            ((strcmp(e->path, "title.bin") == 0 || strcmp(e->path, "title3d.bin") == 0) && strncmp(scen, "FileSelect/", 11) == 0) ||
            (strcmp(e->path, "dome.bin") == 0 && strncmp(scen, "AstroDome/", 10) == 0) ||
            ((strcmp(e->path, "map.bin") == 0 || strcmp(e->path, "mappics.bin") == 0) && strncmp(scen, "AstroGalaxy/", 12) == 0) ||
            (strcmp(e->path, "ending.bin") == 0 && strncmp(scen, "EpilogueDemoStage/", 18) == 0) ||
            strncmp(e->path, "movies/", 7) == 0 || /* any scene may end in one */
            strncmp(e->path, "music/", 6) == 0) {  /* the BGMs (audio.c streams them) */
            if (ntoc == (int)(sizeof toc / sizeof toc[0])) render_fatal("packs: TOC full");
            toc[ntoc++] = e;
        }
    }
    printf("packs: disc TOC %u entries, %d for %s\n", toc_n, ntoc, scen);
}
#else
#define PACK_ALLOC(n) (n)
#endif

#ifdef DISC
/* audio.c's music: a file in PACKS.BIN (its first sector and size), and sectors read only while the drive is idle
 * (the zone prefetch and the movies read asynchronously: the music waits a frame rather than stall them) */
int disc_file(const char *path, u32 *lsn, u32 *size)
{
    for (int i = 0; i < ntoc; i++)
        if (strcmp(toc[i]->path, path) == 0) { *lsn = bin_lsn + toc[i]->off / SECTOR; *size = toc[i]->size; return 1; }
    return 0;
}
int disc_read_idle(u32 lsn, u32 sectors, void *dst)
{
    if (sceCdSync(1)) return 0; /* a read in progress */
    return cd_read(lsn, sectors, dst, 1);
}
/* audio.c's stream: a read started only while the drive is idle (0: busy), not waited for; disc_read_done polls it */
u32 disc_read_async(u32 lsn, u32 sectors, void *dst)
{
    if (sceCdSync(1) || !cd_read(lsn, sectors, dst, 0)) return 0;
    return cd_seq;
}
int disc_read_done(u32 seq) { return seq != cd_seq || !sceCdSync(1); }
#endif

static int pack_open(Pack *p, const char *path)
{
    memset(p, 0, sizeof *p);
#ifdef DISC
    for (int i = 0; i < ntoc; i++)
        if (strcmp(toc[i]->path, path) == 0) {
            p->base = bin_lsn + toc[i]->off / SECTOR;
            p->size = toc[i]->size;
            p->f = (FILE *)1;
            return 1;
        }
    return 0;
#else
    char full[160];
#ifdef PACKS_OVERLAY /* dev builds (XF=-DPACKS_OVERLAY=dir): a pack in host:dir/ wins over packs/'s */
#define OVL_STR2(x) #x
#define OVL_STR(x) OVL_STR2(x)
    snprintf(full, sizeof full, "host:" OVL_STR(PACKS_OVERLAY) "/%s", path);
    if (!(p->f = fopen(full, "rb")))
#endif
    {
        snprintf(full, sizeof full, "host:packs/%s", path);
        if (!(p->f = fopen(full, "rb")))
            return 0;
    }
    fseek(p->f, 0, SEEK_END);
    p->size = ftell(p->f);
    fseek(p->f, 0, SEEK_SET);
    return 1;
#endif
}

/* n bytes at the pack's read position into dst (DISC: whole sectors, the position stays sector aligned) */
static int pack_read(Pack *p, u8 *dst, u32 n)
{
#ifdef DISC
    if (!cd_read(p->base + p->pos / SECTOR, PACK_ALLOC(n) / SECTOR, dst, 1))
        return 0;
#else
    for (u32 done = 0; done < n;) { /* 128 KB at a time: one big host fread now and then never returned (PCSX2) */
        u32 k = n - done < (128u << 10) ? n - done : (128u << 10);
        if (fread(dst + done, 1, k, p->f) != k)
            return 0;
        done += k;
    }
#endif
    p->pos += n;
    return 1;
}

static void pack_close(Pack *p)
{
#ifndef DISC
    if (p->f) fclose(p->f);
#endif
    p->f = NULL;
}

/* menu.c's reads on demand (the map's GalaxyDetail pictures, mappics.bin): n bytes at `off` (DISC: a sector
 * boundary; dst has room for whole sectors) into dst, waited for; 0 when the pack is missing or the read fails */
int packs_read(const char *path, u32 off, void *dst, u32 n)
{
    Pack p;
    if (!pack_open(&p, path)) return 0;
    int ok = off + n <= p.size;
#ifdef DISC
    p.pos = off;
#else
    ok = ok && fseek(p.f, off, SEEK_SET) == 0;
#endif
    ok = ok && pack_read(&p, dst, n);
    pack_close(&p);
    return ok;
}

/* movie.c's reads: in order from one pack (DISC: asynchronous, whole sectors) */
static Pack mv_pack;
#ifdef DISC
static u8 *mv_dst;
static u32 mv_n, mv_lsn;
#endif
static int mv_open(const char *path, u32 *size)
{
    if (!pack_open(&mv_pack, path)) return 0;
    *size = mv_pack.size;
    return 1;
}
static void mv_start(u8 *dst, u32 n)
{
#ifdef DISC
    mv_dst = dst;
    mv_lsn = mv_pack.base + mv_pack.pos / SECTOR;
    mv_n = PACK_ALLOC(n);
    mv_pack.pos += n;
    if (!cd_start(mv_lsn, mv_n / SECTOR, dst)) render_fatal("movie: the disc does not read (sector %u)\n", mv_lsn);
#else
    pack_read(&mv_pack, dst, n);
#endif
}
static int mv_done(void)
{
#ifdef DISC
    if (mv_dst) {
        if (sceCdSync(1)) return 0;
        if (cd_failed(mv_lsn, mv_n / SECTOR)) { /* the chunk read again: played from a stale buffer, libmpeg lost the stream */
            if (!cd_start(mv_lsn, mv_n / SECTOR, mv_dst)) render_fatal("movie: the disc does not read (sector %u)\n", mv_lsn);
            return 0;
        }
        InvalidDCache(mv_dst, mv_dst + mv_n - 1);
        mv_dst = NULL;
    }
#endif
    return 1;
}
static void mv_close(void) { pack_close(&mv_pack); }
static const MovieIo mv_io = {mv_open, mv_start, mv_done, mv_close};

static const u8 *load_file(const char *path)
{
    Pack p;
    if (!pack_open(&p, path)) {
        render_fatal("packs: missing %s\n", path);
    }
    u8 *b = pk_alloc(PACK_ALLOC(p.size));
    if (!b || !pack_read(&p, b, p.size)) {
        render_fatal("packs: cannot read %s (%u B)\n", path, p.size);
    }
    pack_close(&p);
    pack_bytes += p.size;
    return b;
}

static const u8 *zone_files[64];      /* the stage packs in use, one per zone */
static unsigned char zone_full[64];    /* 1: that stage's .bin is in use, 0: its .low.bin */
static const u8 *scene_blob;
static u32 nzones, base_models, base_tex, base_kcl;
static u32 size_full[64], size_low[64]; /* each stage pack's bytes */
static u32 zone_room, zone_room_bg, zone_region; /* what the stage packs may use: in all / beside the packs a background
                                                * swap replaces / at most (the region free: no more fits) */
static u32 resident_bytes;
#define PACK_ROOM 9200000u /* room for scenario data, resident.bin aside (the region less resident.bin, sound.bin's EE part and the menus' packs) (tools/slice_all.py packs to ROOM 9.1e6 less letter.bin, ~75 KB under it, a margin) */
static MemMark base_mark;

/* Next zone's packs, read a slice per frame (a launch star's flight hides it); stages whose pack does not change
 * keep their buffer. ponytail: the old and the new packs are both in RAM until the swap */
#define READ_STEP (32 << 10) /* ~4.5 ms of host: read per frame; 3 MB fits a 300-frame flight */
static struct {
    int active;                        /* a target set is being read */
    u32 z;                             /* next stage to read */
    unsigned char full[64];
    const u8 *buf[64];
    Pack f;           /* f.f != NULL while a pack is being read */
    u8 *cur;
    u32 size, off, bytes;
    u32 flight;       /* DISC: bytes of the asynchronous read in progress (0 none) */
} pend;

static void chain_of(int zone, unsigned char *full)
{
    u32 sz;
    const s32 *parent = (const s32 *)(blob_chunk(scene_blob, "ZPAR", &sz) + 4);
    memset(full, 0, 64);
    for (int z = zone; z >= 0 && z < 64; z = parent[z])
        full[z] = 1;
    full[0] = 1;
}

static void pend_cancel(void)
{
#ifdef DISC
    if (pend.flight) sceCdSync(0); /* the read in progress must land before its buffer goes */
#endif
    if (pend.f.f) pack_close(&pend.f);
    if (pend.cur) pk_free(pend.cur);
    for (u32 z = 0; z < pend.z && z < nzones; z++)
        if (pend.buf[z] && pend.buf[z] != zone_files[z]) pk_free(pend.buf[z]);
    memset(&pend, 0, sizeof pend);
}

static void pend_start(const unsigned char *full)
{
    pend_cancel();
    pend.active = 1;
    memcpy(pend.full, full, 64);
}

static u32 set_cost(const unsigned char *full)
{
    u32 c = 0;
    for (u32 z = 0; z < nzones; z++) c += PACK_ALLOC(full[z] ? size_full[z] : size_low[z]);
    return c;
}

/* The target set: the main stage's chain, then the chains of the zones Mario needs, then of those he is heading for,
 * nearest first, each kept only if the set still fits the room (the nearest needed one not yet in always): those he
 * needs up to the region's room (pend_evict makes way), those he heads for only while the old packs can stay beside
 * them (zone_room_bg: no load pause for a maybe). Petari has every zone of the scenario in memory (a zone's on / off
 * only switches its actors); ponytail: the PS2 region (~9 MB) holds the main chain, the zones around Mario in full and
 * the rest low, not the 10-11 MB of a whole scenario. ponytail: insertion sort over at most 64 zones */
static void target_set(unsigned char *out)
{
    unsigned char need[64], soon[64], chain[64], tmp[64];
    float dist[64];
    int order[64], n = 0;
    game_zone_sets(zone_full, need, soon, dist);
    chain_of(0, out);
    int forced = 0;
    for (int pass = 0; pass < 2; pass++) {
        n = 0;
        for (u32 z = 0; z < nzones; z++)
            if (pass ? soon[z] && !need[z] : need[z]) {
                int k = n++;
                while (k > 0 && dist[order[k - 1]] > dist[z]) { order[k] = order[k - 1]; k--; }
                order[k] = z;
            }
        for (int k = 0; k < n; k++) {
            chain_of(order[k], chain);
            for (int z = 0; z < 64; z++) tmp[z] = out[z] | chain[z];
            if (!memcmp(tmp, out, 64)) continue; /* already in (the main stage's chain) */
            /* the nearest zone he needs and lacks always comes in, the rest only while they fit */
            u32 c = set_cost(tmp); /* (one that can never fit stays low: no collision there, but no hang) */
            if (pass == 0 && !forced ? c <= zone_region : c <= (pass ? zone_room_bg : zone_room)) memcpy(out, tmp, 64);
            forced |= pass == 0;
        }
    }
    for (u32 z = 0; z < nzones; z++) { /* a full zone stays full while it fits: going low only frees room, a swap
                                        * (and a load pause when the region can not hold both) for nothing */
        if (!zone_full[z] || out[z]) continue;
        chain_of(z, chain);
        for (int k = 0; k < 64; k++) tmp[k] = out[k] | chain[k];
        if (set_cost(tmp) <= zone_room) memcpy(out, tmp, 64);
    }
}

static void zones_drop(void) /* the zone models, textures and chains let go (their packs about to be freed) */
{
    render_release(&scene, base_tex, base_models);
    scene.nmodels = base_models;
    scene.ntex = base_tex;
    scene.nkcl = base_kcl;
    mem_release(&base_mark);
}
/* The region can not hold the old and the new packs of a swap at once (OceanPhantomCave: GhostShipCaveZone's 5.6 MB
 * and CaveChangeSizeZone's 1.6 MB): the zone models are dropped and the stage packs the swap replaces freed first, the
 * rest read at once (pend_step), then the swap. 1 if one was freed. ponytail: a load pause where both used to be in
 * RAM (the zones not drawn meanwhile); only for a zone he needs (target_set keeps the background ones in zone_room_bg) */
static int pend_evict(void)
{
    int n = 0;
    for (u32 z = 0; z < nzones; z++)
        if (zone_files[z] && (z < pend.z ? pend.buf[z] != zone_files[z] : pend.full[z] != zone_full[z])) n++;
    if (!n) return 0;
    zones_drop();
    for (u32 z = 0; z < nzones; z++)
        if (zone_files[z] && (z < pend.z ? pend.buf[z] != zone_files[z] : pend.full[z] != zone_full[z])) {
            pk_free(zone_files[z]);
            zone_files[z] = NULL;
        }
    printf("packs: %d stage packs freed before the swap (no room for both)\n", n);
    return 1;
}
/* Still no block for it (the kept packs split the free bytes): every stage pack freed and the whole set read again
 * from the first, packed one after another (as at boot). 1 if any was freed */
static int pend_evict_all(void)
{
    int n = 0;
    if (pend.f.f) pack_close(&pend.f);
    for (u32 z = 0; z < nzones; z++) {
        if (z < pend.z && pend.buf[z] && pend.buf[z] != zone_files[z]) { pk_free(pend.buf[z]); n++; }
        if (zone_files[z]) { pk_free(zone_files[z]); n++; }
        pend.buf[z] = zone_files[z] = NULL;
    }
    if (!n) return 0;
    zones_drop();
    pend.z = 0;
    printf("packs: every stage pack freed, the set read again in one piece\n");
    return 1;
}

/* Read up to `budget` bytes of the pending packs; returns 1 when every stage pack is in memory. */
static int pend_step(u32 budget)
{
    int all = budget == 0xFFFFFFFF; /* read everything now (the budget shrinks as it goes) */
    (void)all;
    char path[160];
    u32 sz;
    const u8 *zn = blob_chunk(scene_blob, "ZONE", &sz);
    while (pend.z < nzones) {
        u32 z = pend.z;
        if (!pend.f.f && !pend.cur) {
            if (zone_files[z] && pend.full[z] == zone_full[z]) { /* unchanged: keep it */
                pend.buf[z] = zone_files[z];
                pend.z++;
                continue;
            }
            snprintf(path, sizeof path, "%s/%s%s", scen, (const char *)(zn + 4 + 56 * z),
                     pend.full[z] ? ".bin" : ".low.bin");
            if (!pack_open(&pend.f, path)) {
                render_fatal("packs: missing %s" "\n", path);
            }
            pend.size = pend.f.size;
            pend.cur = pk_alloc(PACK_ALLOC(pend.size));
            if (!pend.cur && pend_evict()) { /* then all of it now: nothing of those zones is left to draw */
                budget = 0xFFFFFFFF;
                all = 1;
                pend.cur = pk_alloc(PACK_ALLOC(pend.size));
            }
            if (!pend.cur && pend_evict_all()) {
                budget = 0xFFFFFFFF;
                all = 1;
                continue;
            }
            if (!pend.cur) {
                printf("packs: no memory for %s (%u KB)\n", path, pend.size >> 10);
                for (int i = 0; i < pk_n; i++) printf("packs:   block %u KB %s\n", pk_blk[i].size >> 10, pk_blk[i].used ? "used" : "free");
                render_fatal("packs: no memory for\n%s (%u KB)\nthe pack region is %u KB (tools/zone_fit.py --elf)", path, pend.size >> 10, pk_size >> 10);
            }
            pend.off = 0;
        }
#ifdef DISC
        if (pend.flight) { /* the asynchronous read: still going, or in */
            if (!all && sceCdSync(1))
                return 0;
            sceCdSync(0);
            if (cd_failed(pend.f.base + pend.off / SECTOR, PACK_ALLOC(pend.flight) / SECTOR)) {
                if (!cd_read(pend.f.base + pend.off / SECTOR, PACK_ALLOC(pend.flight) / SECTOR, pend.cur + pend.off, 0))
                    render_fatal("packs: read error" "\n");
                continue; /* polled again */
            }
            InvalidDCache(pend.cur + pend.off, pend.cur + pend.off + PACK_ALLOC(pend.flight) - 1);
            pend.off += pend.flight;
            pend.f.pos += pend.flight;
            pend.bytes += pend.flight;
            pend.flight = 0;
            if (!all) budget = 0; /* one read per frame */
        } else if (budget && !all && pend.off < pend.size) { /* start the next one, poll it later */
            u32 n = pend.size - pend.off < budget ? pend.size - pend.off : budget;
            if (!cd_read(pend.f.base + pend.off / SECTOR, PACK_ALLOC(n) / SECTOR, pend.cur + pend.off, 0)) {
                render_fatal("packs: read error" "\n");
            }
            pend.flight = n;
            return 0;
        }
        if (pend.off == pend.size) {
            pack_close(&pend.f);
            pend.buf[z] = pend.cur;
            pend.cur = NULL;
            pend.z++;
            continue;
        }
#endif
        if (!budget)
            return 0;
        u32 n = pend.size - pend.off < budget ? pend.size - pend.off : budget;
        if (!pack_read(&pend.f, pend.cur + pend.off, n)) {
            render_fatal("packs: read error" "\n");
        }
        pend.off += n;
        pend.bytes += n;
        budget -= n;
        if (pend.off == pend.size) {
            pack_close(&pend.f);
            pend.buf[z] = pend.cur;
            pend.cur = NULL;
            pend.z++;
        }
    }
    return 1;
}

/* Swap to the pending zone (its packs all read): drop the zone models, textures and chains, parse the packs. */
static void pend_commit(Scene *s)
{
    u32 t0 = cycles();
    render_release(s, base_tex, base_models);
    for (u32 z = 0; z < nzones; z++) {
        if (zone_files[z] && zone_files[z] != pend.buf[z]) pk_free(zone_files[z]);
        zone_files[z] = pend.buf[z];
    }
    memcpy(zone_full, pend.full, sizeof zone_full);
    s->nmodels = base_models;
    s->ntex = base_tex;
    s->nkcl = base_kcl;
    mem_release(&base_mark);
    for (u32 z = 0; z < nzones; z++)
        if (blob_add(s, zone_files[z]) != 0) {
            printf("packs: zone pack %u (%s) starts %.4s\n", z, zone_full[z] ? "full" : "low", (const char *)zone_files[z]);
            render_fatal("packs: zone %u failed" "\n", z);
        }
    render_load_textures(s, base_tex, base_models);
    game_rebind();
    int nfull = 0;
    for (u32 z = 0; z < nzones; z++) nfull += zone_full[z];
    printf("packs: %d full zones (%u KB), %u models, %u textures, %u KB read before, swap %u us" "\n", nfull, set_cost(zone_full) >> 10,
           s->nmodels, s->ntex, pend.bytes >> 10, (u32)((cycles() - t0) / 294.912f));
    memset(&pend, 0, sizeof pend);
}

/* Per frame: prepare the zone the game expects next, swap when it is wanted (reading the rest at once if needed). */
static void stream_update(Scene *s)
{
    unsigned char target[64], need[64], soon[64];
    float dist[64];
    target_set(target);
    game_zone_sets(zone_full, need, soon, dist);
    int urgent = 0; /* a zone he needs now is low: read it all at once */
    for (u32 z = 0; z < nzones; z++)
        if (target[z] && need[z] == 2 && !zone_full[z]) urgent = 1;
    if (!memcmp(target, zone_full, nzones)) {
        if (pend.active) pend_cancel();
        return;
    }
    if (!pend.active || memcmp(pend.full, target, nzones))
        pend_start(target);
    if (pend_step(urgent ? 0xFFFFFFFF : READ_STEP))
        pend_commit(s);
}

static const u8 *base_bufs[10]; /* scene.bin, resident.bin, sound.bin, common.bin, menus(3d).bin...: freed at a scene change */
static int nbase;
static const u8 *load_base(const char *path) { return base_bufs[nbase++] = load_file(path); }

/* GameSystemStationedArchiveLoader::createAndAddPlayerArchives: the player's archives by the file's player (Mario.arc /
 * Luigi.arc): resident_luigi.bin (Luigi's model and clips) instead of resident.bin when it plays as Luigi */
static const char *resident_pack(void)
{
    Pack p;
    if (!gd_player_luigi() || !pack_open(&p, "resident_luigi.bin")) return "resident.bin";
    pack_close(&p);
    return "resident_luigi.bin";
}
/* LuigiLetter::makeArchiveListForMenu / ForNPC: letter.bin only where the pause menu's or the messenger's letter can show */
static int letter_scene(void)
{
    char st[48];
    const char *sl = strchr(scen, '/');
    snprintf(st, sizeof st, "%.*s", sl ? (int)(sl - scen) : (int)strlen(scen), scen);
    int no = sl ? sl[1] - '0' : -1;
    return gd_luigi_letter_galaxy(st, no) || (!strcmp(st, "AstroGalaxy") && gd_luigi_letter_galaxy_npc(st, no));
}

static const u8 *load_packs(Scene *s)
{
    char path[160];
#ifdef DISC
    toc_load();
    toc_select();
#endif
    snprintf(path, sizeof path, "%s/scene.bin", scen);
    scene_blob = load_base(path);
    if (blob_load(s, scene_blob) != 0)
        return NULL;
    u32 before_resident = pack_bytes;
    if (blob_add(s, load_base(resident_pack())) != 0)
        return NULL;
    { /* the scenario's sound pack (tools/sound.py --pack --scene), else the shared one */
        Pack sp;
        snprintf(path, sizeof path, "%s/sound.bin", scen);
        int own = pack_open(&sp, path);
        if (own) pack_close(&sp);
        u32 before = pack_bytes;
        const u8 *sb = load_base(own ? path : "sound.bin");
        se_load(sb, gd_player_luigi());
        u32 keep = se_compact(sb, pack_bytes - before); /* the ADPCM went to SPU RAM: its EE copy freed */
        pk_shrink(sb, PACK_ALLOC(keep));
        pack_bytes = before + keep;
    }
    resident_bytes = pack_bytes - before_resident; /* its own budget (milestone 1), not the scenario room */
    snprintf(path, sizeof path, "%s/common.bin", scen);
    if (blob_add(s, load_base(path)) != 0)
        return NULL;
    { /* the menus' layouts (tools/lyt.py), when built: outside the scenario room */
        Pack mp;
        /* layouts / messages, their 3D models; the title and file select's (FileSelect); the dome's (AstroDome); the
         * Observatory's map (AstroGalaxy); the ending's staff roll (EpilogueDemoStage) */
        static const char *const mp_names[7] = {"menus.bin", "menus3d.bin", "title.bin", "title3d.bin", "dome.bin", "map.bin", "ending.bin"};
        static const char *const mp_scene[7] = {"", "", "FileSelect/", "FileSelect/", "AstroDome/", "AstroGalaxy/", "EpilogueDemoStage/"};
        for (int k = 0; k < 7; k++)
            if (strncmp(scen, mp_scene[k], strlen(mp_scene[k]))) continue; /* (the empty prefix: every scene) */
            else
            if (pack_open(&mp, mp_names[k])) {
                pack_close(&mp);
                u32 before = pack_bytes;
                if (blob_add(s, load_base(mp_names[k])) != 0) return NULL;
                resident_bytes += pack_bytes - before; /* its own budget, as resident.bin's */
            }
        if (letter_scene() && pack_open(&mp, "letter.bin")) { /* Luigi's letter */
            pack_close(&mp);
            u32 before = pack_bytes;
            if (blob_add(s, load_base("letter.bin")) != 0) return NULL;
            resident_bytes += pack_bytes - before;
        }
    }
    u32 sz;
    nzones = *(const u32 *)blob_chunk(scene_blob, "ZONE", &sz);
    if (nzones > 64)
        return NULL;
    base_models = s->nmodels;
    base_tex = s->ntex;
    base_kcl = s->nkcl;
    const u8 *zn = blob_chunk(scene_blob, "ZONE", &sz);
    for (u32 z = 0; z < nzones; z++) { /* each stage pack's size, full and low (the room check in target_set) */
        Pack p;
        for (int lo = 0; lo < 2; lo++) {
            snprintf(path, sizeof path, "%s/%s%s", scen, (const char *)(zn + 4 + 56 * z), lo ? ".low.bin" : ".bin");
            u32 *dst = lo ? &size_low[z] : &size_full[z];
            *dst = pack_open(&p, path) ? p.size : 0;
            if (p.f) pack_close(&p);
        }
    }
    u32 scen_base = pack_bytes - resident_bytes; /* scene.bin + common.bin: tools/slice_all.py's stream count */
    zone_room = PACK_ROOM > scen_base ? PACK_ROOM - scen_base : 0;
    { /* and what the region really has left (its size follows the ELF's; the menus' packs came out of it). A swap in
       * the background holds the old packs while the new ones come in, so for the zones he only heads for the low
       * packs' sum is kept aside (zone_room_bg); a zone he needs may take it all (pend_evict).
       * ponytail: free bytes summed, not the largest free block (pend_evict_all packs the set again when split) */
        u32 left = 0, lows = 0;
        for (int i = 0; i < pk_n; i++) if (!pk_blk[i].used) left += pk_blk[i].size;
        for (u32 z = 0; z < nzones; z++) lows += PACK_ALLOC(size_low[z]);
        zone_region = left;
        if (left < zone_room) { printf("packs: zone room %u KB (the region's), not %u KB\n", left >> 10, zone_room >> 10); zone_room = left; }
        zone_room_bg = zone_room > lows ? zone_room - lows : 0;
        for (u32 z = 0; z < nzones; z++) { /* a zone whose chain (the rest low) is over the region: it can not load (slice_all.py room, tools/zone_fit.py) */
            unsigned char chain[64];
            chain_of(z, chain);
            u32 c = set_cost(chain);
            if (c > zone_region)
                printf("packs: zone %s never fits: %u KB in full, region %u KB (it stays low)\n", (const char *)(zn + 4 + 56 * z), c >> 10, zone_region >> 10);
        }
    }
    printf("packs: %s, %u zones (room %u KB, %u KB in the background), base %u models, %u textures, %u KB read\n", scen, nzones, zone_room >> 10, zone_room_bg >> 10, s->nmodels, s->ntex,
           pack_bytes >> 10);
    return scene_blob;
}
#else
int packs_read(const char *path, u32 off, void *dst, u32 n) { (void)path; (void)off; (void)dst; (void)n; return 0; }
#endif

#define EE_MHZ 294.912f

static void load_modules(void)
{
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    SifLoadModule("rom0:XSIO2MAN", 0, NULL); /* X modules (libpadx): see mc.c mc_init */
    SifLoadModule("rom0:XPADMAN", 0, NULL);
    mc_init(); /* LogoScene MountGameData: startPreLoadSaveDataSequence (its windows: the first scene's menu.c sv_boot) */
#if !defined(TITLE_TEST) && !defined(NEWFILE_SEED) /* (the title test and newfile's seed start from no files) */
    int sz, res = mc_load(gd_image(&sz), sz);
    if (res == MC_OK) gd_image_loaded();
    mc_set_boot(res);
#endif
}

#if !defined(TEASER) && (defined(BENCH) || defined(DEBUG_HUD) || !defined(CATCHUP)) /* the text lines */
static void ms(char *out, int n, const char *label, u32 c)
{
    u32 us = (u32)(c / EE_MHZ);
    snprintf(out, n, "%s %u.%02u", label, us / 1000, (us % 1000) / 10);
}
#endif

#ifdef CATCHUP
static volatile u32 vblanks; /* vblank starts (INTC_VBLANK_S) */
static int vblank_tick(int cause) { (void)cause; vblanks++; ExitHandler(); return 0; }
#endif

/* the next scene asked by the game (game.h game_request_scene: "<Galaxy>/<n>"); taken at the end of the frame's update */
static char scene_next[64];
void game_request_scene(const char *next) { snprintf(scene_next, sizeof scene_next, "%s", next); }
#ifdef PACKS
#define SCENE_ASKED() (scene_next[0] != 0)
#else /* (no scene changes: the request is never taken) */
#define SCENE_ASKED() 0
#endif
/* a movie played before the next scene loads (MoviePlayingSequence: game.h game_request_movie) */
static char movie_next[64]; /* names, ',' between */
void game_request_movie(const char *name) { snprintf(movie_next, sizeof movie_next, "%s", name); }

#ifdef PACKS
/* A scene in place (MR::requestChangeScene / StorySequenceExecutor: the next stage): the game modules' static state
 * back to the ELF's (their gamedata restored from the boot copy, gamebss cleared: Makefile GAME_OBJS), the arena back to
 * the boot mark, every pack and texture let go; then the scene of `scen` loads as at boot */
extern char __start_gamedata[], __stop_gamedata[], __start_gamebss[], __stop_gamebss[];
static char *gamedata_boot;
static MemMark boot_mark;
static void scene_load(void);
static void pend_cancel(void);

/* the movies asked (game_request_movie), with no scene loaded: subs.bin (tools/lyt.py: MovieSubtitles' CinemaSuper,
 * its messages and their glyphs, the movies' rumbles) at the bottom of the packs' region as the scene for lyt.c,
 * msg_get and the talk font, the movies' buffers above it; all of it let go after */
static void movies_play(void)
{
    MemMark mm;
    mem_mark(&mm);
    pk_movie = 1;
    Pack sp;
    const u8 *subs = NULL;
    if (pack_open(&sp, "subs.bin")) { pack_close(&sp); subs = load_file("subs.bin"); }
    if (subs && blob_add(&scene, subs) == 0) {
        render_load_textures(&scene, 0, 0);
        lyt_init(&scene);
        game_font_init(&scene);
        msg_set(scene.msgs);
    }
    for (char *m = strtok(movie_next, ","); m; m = strtok(NULL, ",")) {
        movie_play(m, &mv_io, subs ? blob_chunk(subs, "MRUM", NULL) : NULL);
        render_release(&scene, 0, 0); /* the font texture uploaded again past the next picture */
    }
    pk_free(subs);
    msg_set(NULL);
    memset(&scene, 0, sizeof scene);
    pack_bytes = 0;
    pk_movie = 0;
    mem_release(&mm);
    movie_next[0] = 0;
}

static void scene_switch(void)
{
    printf("scene: %s -> %s\n", scen, scene_next);
    me_reset(); /* AudSystem::preProcessToReset */
    se_silence();
    audio_stop(); /* its FILE sits above the base packs: freed before they go (the heap stays whole) */
    if (pend.active) pend_cancel();
    render_release(&scene, 0, 0);
    for (u32 z = 0; z < 64; z++) {
        pk_free(zone_files[z]);
        zone_files[z] = NULL;
    }
    for (int i = 0; i < nbase; i++) pk_free(base_bufs[i]);
    nbase = 0;
    memset(&scene, 0, sizeof scene);
    memset(zone_full, 0, sizeof zone_full);
    scene_blob = NULL;
    nzones = base_models = base_tex = base_kcl = 0;
    pack_bytes = resident_bytes = 0;
    mem_release(&boot_mark);
    memcpy(__start_gamedata, gamedata_boot, __stop_gamedata - __start_gamedata);
    memset(__start_gamebss, 0, __stop_gamebss - __start_gamebss);
    strcpy(scen, scene_next);
    scene_next[0] = 0;
    pad_rumble_stop(); /* GameSystemSceneController: resetAllControllerRumble */
    if (movie_next[0]) movies_play();
    scene_load();
}
#endif

#ifdef PACKS
static void scene_load(void)
{
    const u8 *blob = load_packs(&scene);
    if (!blob) {
        render_fatal("slice: bad blob\n");
    }
    /* base packs first, then the game (its allocations stay), then the zone packs above the mark */
    render_load_textures(&scene, 0, 0);
    game_stage_name = scen;
    game_init(&scene, blob);
    mem_mark(&base_mark);
    game_set_piece_follow_limit(!strcmp(scen, "EggStarGalaxy/2"));
    { /* CometEventKeeper::sTimeLimitInfoTable: the Red / Black comet scenarios' limits (seconds) */
        static const struct { const char *scen; int secs; } limits[] = {
            {"EggStarGalaxy/4", 240}, {"StarDustGalaxy/4", 240}, {"SandClockGalaxy/4", 270}, {"CannonFleetGalaxy/4", 360},
            {"StarDustGalaxy/5", 120}, {"PhantomGalaxy/5", 60}, {"CosmosGardenGalaxy/5", 150}, {"FactoryGalaxy/5", 180},
            {"ReverseKingdomGalaxy/5", 210}};
        for (u32 i = 0; i < sizeof limits / sizeof limits[0]; i++)
            if (!strcmp(limits[i].scen, scen)) { game_set_time_limit(limits[i].secs); printf("time limit %d s\n", limits[i].secs); }
    }
    game_bgm_stage(); /* the scenario's stage BGM (game.c: music_tab.h) */
    {
        unsigned char start[64], near[64]; /* the start zone's chain and what streaming wants around Mario there (OceanRing 4
                                            * starts over another zone: without it, no ground for 2 s) */
        chain_of(scene_start(&scene)->zone, start);
        target_set(near);
        for (int z = 0; z < 64; z++) start[z] |= near[z];
        pend_start(start);
    }
    pend_step(0xFFFFFFFF);
    pend_commit(&scene);
}
#endif

int main(void)
{
#ifdef PACKS
    gamedata_boot = malloc(__stop_gamedata - __start_gamedata); /* before anything writes it */
    memcpy(gamedata_boot, __start_gamedata, __stop_gamedata - __start_gamedata);
#endif
    load_modules();
    audio_init();
    se_init();
    render_init();
    pad_init();
#ifdef PACKS
    pick_scenario();
    pk_init();
    mem_mark(&boot_mark);
    scene_load();
#else
    const u8 *blob = blob_load(&scene, slice_blob) == 0 ? slice_blob : NULL;
    if (!blob) {
        render_fatal("slice: bad blob\n");
    }
    render_load_textures(&scene, 0, 0);
    game_init(&scene, blob);
#endif

    mem_report();

    u32 t_logic = 0, t_draw = 0, t_gs = 0, worst = 0, frame = 0; (void)t_gs;
#ifdef BENCH
    /* frames 60..1859 of the scripted path: sums and worsts of logic (minus KCL), KCL, draw, GS finish, total */
    u64 sum[5] = {0};
    u32 top[5] = {0};
#endif
#if !defined(TEASER) && (defined(BENCH) || defined(DEBUG_HUD) || !defined(CATCHUP)) /* the text lines */
    char line[128], a[24], b[24], c[24];
#endif
#ifdef BENCH
    { /* mat_mul on VU0 against the C one */
        float x[16], y[16], r1[16], r2[16], worst = 0;
        for (int t = 0; t < 200; t++) {
            for (int i = 0; i < 16; i++) { x[i] = (float)((t * 7 + i * 13) % 97) / 9.0f - 5.0f; y[i] = (float)((t * 11 + i * 5) % 89) / 7.0f - 6.0f; }
            mat_mul(r1, x, y); mat_mul_c(r2, x, y);
            for (int i = 0; i < 16; i++) { float d = fabsf(r1[i] - r2[i]) / (1.0f + fabsf(r2[i])); if (d > worst) worst = d; }
        }
        printf("bench mat_mul VU0 vs C: worst relative difference %f" "\n", worst);
    }
#endif
#ifdef CATCHUP
    /* the game's logic at a fixed 60 Hz (Petari's frame): every vblank since the last frame is one logic step (at most
     * 2: a longer frame is let go), the pad read before each (its presses count once); then one draw, its clips,
     * sounds and wipe advancing those steps */
    AddIntcHandler(INTC_VBLANK_S, vblank_tick, 0);
    EnableIntc(INTC_VBLANK_S);
    u32 last_vb = vblanks;
#endif
    for (;;) {
        u32 t0 = cycles();
#if defined(BENCH) && defined(DISC)
        if (frame >= 1860) print_pace_on = 1;
#endif
        game_kcl_cycles = 0;
        int steps = 1;
#ifdef CATCHUP
        steps = (int)(vblanks - last_vb);
        last_vb = vblanks;
        if (steps < 1) steps = 1;
        if (steps > 3) steps = 3; /* real speed down to 20 fps (the console runs 20-30 in heavy scenes: with 2, Mario
                                    * moved in slow motion); a longer frame slows the game a moment rather than spiralling */
        /* (no step after a scene change was asked: the old scene's logic would act on the next one's game data) */
        for (int s = 1; s < steps && !SCENE_ASKED(); s++) { pad_update(); game_update(); audio_feed(); }
#endif
        pad_update();
        if (!SCENE_ASKED()) game_update();
        audio_feed(); /* audsrv holds only 10 KB (80 ms of a stream): fed between the frame's long parts too */
        if (menu_take_save_request()) { int sz; mc_save(gd_image(&sz), sz); } /* SaveDataHandleSequence: save all */
        mc_poll();
#ifdef PACKS
        if (scene_next[0]) {
            scene_switch();
#ifdef CATCHUP
            last_vb = vblanks;
#endif
            continue;
        }
#endif
#if defined(PACKS) && defined(ASTRO_TEST)
        if (frame == 200) { /* the first grand star (the first dome open), Mario in a dome's entrance cube */
            gd_set_star("HeavensDoorGalaxy", 1);
            printf("astro test: grand star 1 %d, cube row %d\n", gd_flag_is_on("SpecialStarGrand1"), game_test_astro_cube(5));
        }
#endif
#if defined(PACKS) && defined(ENDING_TEST)
        { /* KoopaBattleVs3Galaxy/1's star faked: EpilogueA, EpilogueDemoStage's エピローグデモ, EndingA, the staff roll, its
           * information windows, the save (Yes), the title. X held every second (the talks' pages, the windows) */
            pad_force = frame > 300 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (menu_saving()) menu_test_point("BoxRight");
            if (frame == 200) {
                SeqResult r = {"KoopaBattleVs3Galaxy", 1, 0, 1, 0, 0, 0};
                r.prev_stars = gd_star_num();
                gd_set_star("KoopaBattleVs3Galaxy", 1);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(1);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
            }
            if (frame > 200 && frame % 120 == 0)
                printf("ending test f %u: busy %d demo %d staff roll %d pause %d saving %d\n", frame, game_story_busy(), game_test_demo(-1),
                       menu_staffroll_on(), menu_staffroll_pause_or_end(), menu_saving());
        }
#endif
#if defined(PACKS) && defined(RESULT_TEST)
        { /* a stage clear faked in EggStar (its star 1, 50 coins, 12 star pieces): the dome, the result, the save */
            pad_force = 0;
            if (frame == 200) {
                SeqResult r = {"EggStarGalaxy", 1, 0, 1, 50, 0, 12};
                gd_set_star("HeavensDoorGalaxy", 1);
                r.prev_stars = gd_star_num();
                gd_set_star("EggStarGalaxy", 1);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(1);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
            }
            if (frame > 600 && frame % 90 == 0) {
                menu_test_point("BoxRight");
                pad_force = PAD_CROSS;
                printf("result test f %u: X, result %d saving %d mario %d\n", frame, menu_result_showing(), menu_saving(), game_test_demo(-3));
            }
        }
#endif
#if defined(PACKS) && defined(REPORT_TEST)
        { /* EggStar's star 2 faked: the dome, its star return, then バトラー報告 (cDemoNewGalaxyDiscover) */
            pad_force = frame > 600 && frame % 60 < 10 ? PAD_CROSS : 0; /* the result's pages and the Butler's */
            if (frame == 200) {
                SeqResult r = {"EggStarGalaxy", 2, 0, 1, 50, 0, 12};
                gd_set_star("HeavensDoorGalaxy", 1);
                gd_set_star("EggStarGalaxy", 1);
                gd_story_follow("バトラー情報Ａ"); gd_story_follow("スターピースレクチャー");
                r.prev_stars = gd_star_num();
                gd_set_star("EggStarGalaxy", 2);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(2);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
            }
            if (frame > 200 && frame % 60 == 0 && frame < 4000)
                printf("report test f %u: busy %d demo %d result %d saving %d\n", frame, game_story_busy(), game_test_demo(-1),
                       menu_result_showing(), menu_saving());
        }
#endif
#if defined(PACKS) && defined(BUTLER_TEST)
        { /* AstroDome/1: Mario before the Butler, X: its own talk (AstroDome_Butler000), X held every second for the pages */
            if (frame >= 200 && frame < 230) game_test_butler_talk(1);
            pad_force = frame > 240 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (frame >= 200 && (frame % 30 == 0 || (frame > 236 && frame < 262)) && frame < 1500) printf("butler test f %u: talk %d\n", frame, game_test_butler_talk(0));
        }
#endif
#if defined(PACKS) && defined(ASTRODEMO_TEST)
        { /* TriLegLv1Galaxy/1: its grand star (2) faked -> AstroGalaxy: グランドスター２帰還, ロゼッタトーチの炎説明デモ,
           * バトラーマップレクチャー (cDemoTorchLecture); BattleShipGalaxy/1: BattleShip's star 7, the first green star, faked
           * -> AstroGalaxy: パワースター帰還, バトラーグリーンドライバ説明 (cDemoGreenStarGetFirst). X held every second */
            int green = !strncmp(PACKS, "BattleShip", 10);
            pad_force = frame > 600 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (menu_saving()) menu_test_point("BoxRight"); /* the save prompt: Yes */
            if (menu_galaxy_map_active() && frame % 120 == 0) pad_force = PAD_CIRCLE; /* the map shown by the lectures: closed */
            if (frame == 200) {
                SeqResult r = {"TriLegLv1Galaxy", 1, 0, 1, 50, 0, 12};
                if (green) { strcpy(r.stage, "BattleShipGalaxy"); r.star = 7; }
                gd_set_star("HeavensDoorGalaxy", 1);
                gd_story_follow("バトラー情報Ａ"); gd_story_follow("スターピースレクチャー");
                if (green) gd_set_star("TriLegLv1Galaxy", 1);
                r.prev_stars = gd_star_num();
                gd_set_star(r.stage, r.star);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(r.star);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
            }
            if (frame > 200 && frame % 60 == 0 && frame < 9000)
                printf("astrodemo test f %u: busy %d demo %d result %d saving %d map %d\n", frame, game_story_busy(), game_test_demo(-1),
                       menu_result_showing(), menu_saving(), menu_galaxy_map_active());
        }
#endif
#if defined(PACKS) && defined(SPIECE_TEST)
        { /* EggStar's star 1 faked (the dome lecture heard): the star return, the result, スターピース解説前半, five star
           * pieces at the Butler, スターピース解説後半 */
            static int shots;
            pad_force = frame > 600 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (menu_saving()) menu_test_point("BoxRight"); /* the save prompt: Yes */
            if (frame == 200) {
                SeqResult r = {"EggStarGalaxy", 1, 0, 1, 50, 0, 12};
                gd_set_star("HeavensDoorGalaxy", 1);
                gd_story_follow("バトラー情報Ａ");
                r.prev_stars = gd_star_num();
                gd_set_star("EggStarGalaxy", 1);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(1);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
            }
            if (frame > 600 && frame % 90 == 45 && !game_story_busy() && game_test_demo(-1) < 0 && shots < 12) {
                pad_force = 0;
                printf("spiece test f %u: shot, hits %d\n", frame, game_test_butler_shoot());
                shots++;
            }
            if (frame > 200 && frame % 60 == 0 && frame < 5000)
                printf("spiece test f %u: busy %d demo %d lecture %d saving %d guidance %d counter %d\n", frame, game_story_busy(),
                       game_test_demo(-1), gd_story_passed("スターピースレクチャー"), menu_saving(), menu_guidance_exists(), game_test_spc());
        }
#endif
#if defined(PACKS) && defined(ROSETTA_TEST)
        { /* AstroGalaxy/4: ロゼッタ状況説明デモ by name (the grand star return before it is another port), X every second for
           * the talks' pages and the picture book; once it ends, ロゼッタ天文台機能回復デモ (a talk demo) */
            static int phase;
            pad_force = frame > 300 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (frame == 300) printf("rosetta test: start %d\n", game_test_demo_name("ロゼッタ状況説明デモ"));
            if (frame > 600 && phase == 0 && game_test_demo(-1) < 0) {
                phase = 1;
                printf("rosetta test f %u: second %d\n", frame, game_test_demo_name("ロゼッタ天文台機能回復デモ"));
            }
            if (frame > 300 && frame % 60 == 0 && frame < 6000)
                printf("rosetta test f %u: demo %d book %d rosetta %d\n", frame, game_test_demo(-1), menu_monologue_alive(), game_test_demo(-4));
        }
#endif
#if defined(PACKS) && defined(GRAND_TEST)
        { /* HeavensDoorGalaxy/1: its grand star faked -> AstroGalaxy/4, グランドスター１帰還 (then ロゼッタ状況説明デモ);
           * HeavensDoorGalaxy/2: its star 2 faked -> AstroGalaxy, パワースター帰還. X held every second: the result's pages */
            static int pages;
            int second = !strcmp(PACKS, "HeavensDoorGalaxy/2");
            pad_force = frame > 600 && frame % 60 < 10 ? PAD_CROSS : 0;
            if (menu_saving()) menu_test_point("BoxRight"); /* the save prompt: Yes */
            if (frame == 200) {
                SeqResult r = {"HeavensDoorGalaxy", second ? 2 : 1, 0, 1, 50, 0, 12};
                if (second) gd_set_star("HeavensDoorGalaxy", 1);
                r.prev_stars = gd_star_num();
                gd_set_star("HeavensDoorGalaxy", second ? 2 : 1);
                r.after_stars = gd_star_num();
                seq_set_result(&r);
                seq_set_cleared(second ? 2 : 1);
                seq_galaxy_move(SEQ_STAGE_CLEAR, NULL, -1, -1, 0);
                printf("grand test: grand star 1 flag %d\n", gd_flag_is_on("SpecialStarGrand1"));
            }
            if (frame > 200 && frame % 60 == 0 && frame < 6000)
                printf("grand test f %u: busy %d demo %d result %d saving %d\n", frame, game_story_busy(), game_test_demo(-1),
                       menu_result_showing(), menu_saving());
            (void)pages;
        }
#endif
#if defined(PACKS) && defined(FINALMOVIE_TEST) /* AstroGalaxy/5: the FinalBattle MovieStarter, then the scenario select */
        if (frame == 200) printf("finalmovie test: movie starter %d\n", game_test_movie(2));
        if (frame == 400) printf("finalmovie test: scenario select %d\n", menu_scenario_selecting());
#endif
#if defined(PACKS) && defined(DEMO_TEST) /* EggStarGalaxy/1: Gã‚­ãƒ£ãƒ—ãƒãƒ£ãƒ¼å‡ºç¾ (BlueStarZone's SW 1: the blue chips) */
        if (frame == 1300) printf("demo test: start %d\n", game_test_demo(0));
        if (frame > 1300 && frame % 20 == 0) printf("demo test f %u: %d\n", frame, game_test_demo(-1));
#endif
#if defined(PACKS) && defined(STORY_TEST) /* a fresh file moved to AstroDome/1: ドームレクチャー１ (cDemoAstroDomeLecture),
                                           * then R2 at the blue star: the galaxy select's ドームレクチャー２ */
        if (frame == 200) seq_galaxy_move(SEQ_MOVE, "AstroDome", 1, -1, 0);
        pad_force = frame > 300 && frame % 60 < 10 ? PAD_CROSS : 0; /* the Butler's pages (held: TalkStateEvent turns on a hold) */
        if (frame >= 1500 && frame < 1545) game_test_dome(0, 1);
        if (frame == 1545) game_test_dome(0, 0);
        if (frame > 200 && frame % 30 == 0 && frame < 3000)
            printf("story test f %u: busy %d demo %d butlerA %d lecture %d\n", frame, game_story_busy(), game_test_demo(-1),
                   gd_story_passed("バトラー情報Ａ"), gd_story_passed("天球儀レクチャー"));
#endif
#if defined(PACKS) && defined(SHELL_TEST) /* HellProminenceGalaxy/1: 貝殻惑星[開始] (demo 1): the LavaShellTower's three anim
                                           * cameras, the stage BGM stopped (60) then MBGM_GALAXY_02_HURRY, the scene stopped */
        if (frame == 300) printf("shell test: start %d\n", game_test_demo(1));
        if (frame > 300 && frame % 30 == 0 && frame < 1500)
            printf("shell test f %u: demo %d, anim camera frame %d\n", frame, game_test_demo(-1), game_test_demo(-2));
#endif
#if defined(PACKS) && defined(GATEWAY_TEST)
        pad_force = game_test_gateway(frame) && frame % 40 < 3 ? PAD_CROSS : 0; /* A tapped while a talk waits */
#endif
#if defined(PACKS) && defined(SPINGATE_TEST)
        { /* the spin's gate (スピン権利): Square tapped every 200 frames while Mario is free, his spin timer 2 frames
           * after (0: no spin). As `gateway` with XF=-DSPINGATE_TEST on HeavensDoorGalaxy/1: none until the spin is explained */
#ifndef GATEWAY_TEST
            pad_force = 0;
#endif
            if (game_test_free() && frame % 200 < 3) pad_force |= PAD_SQUARE;
            if (game_test_free() && frame % 200 == 4) printf("spingate test f %u: spin timer %d\n", frame, game_test_spin_timer());
        }
#endif
#if defined(PACKS) && defined(PROLOGUE_TEST)
        if (frame == 300 || frame == 700) printf("prologue test f %u: movie starter %d\n", frame, game_test_movie(frame == 700));
#endif
#if defined(PACKS) && defined(MAP_TEST)
        { /* the Observatory's map (XF=-DMAP_MODE=n: GalaxyMapController's mode, 0 Astro; a discover mode closes at
           * the first X): open, Good Egg's detail with its picture (X on its icon, X), the other view (X on nothing),
           * the star list, back, close */
#ifndef MAP_MODE
#define MAP_MODE 0
#endif
            pad_force = 0;
            if (frame == 200) { gd_set_star("EggStarGalaxy", 1); gd_flag_try_on("EggStarGalaxy"); }
            if (frame == 300) { menu_galaxy_map(MAP_MODE); printf("map test f %u: open %d (mode %d)\n", frame, menu_galaxy_map_active(), MAP_MODE); }
            if (frame == 400) printf("map test f %u: pointer on EggStarGalaxy %d\n", frame, menu_test_point_map("EggStarGalaxy"));
            if (frame == 420 || frame == 560) pad_force = PAD_CROSS;
            if (frame == 640) game_set_cursor(40, 40);
            if (frame == 660) pad_force = PAD_CROSS;
            if (frame == 840) printf("map test f %u: pointer on MapList %d\n", frame, menu_test_point_map("BoxLButton"));
            if (frame == 860) pad_force = PAD_CROSS;
            if (frame == 1100 || frame == 1300) pad_force = PAD_CIRCLE;
            if (frame == 1400) printf("map test f %u: open %d\n", frame, menu_galaxy_map_active());
        }
#endif
#if defined(PACKS) && defined(COPY_TEST)
        { /* the file select with file 1 saved: file 1, Copy, file 2, Yes */
            static const struct { u32 f; int slot; const char *pane; u32 btn; } st[] = {
                {400, -1, NULL, PAD_CROSS | PAD_CIRCLE}, {600, 0, NULL, 0}, {620, -1, NULL, PAD_CROSS}, {820, -1, "PicCopy", 0},
                {840, -1, NULL, PAD_CROSS}, {1000, 1, NULL, 0}, {1020, -1, NULL, PAD_CROSS}, {1150, -1, "BoxRight", 0},
                {1170, -1, NULL, PAD_CROSS}};
            pad_force = 0;
            for (unsigned k = 0; k < sizeof st / sizeof st[0]; k++)
                if (frame >= st[k].f && frame < st[k].f + (st[k].btn == (PAD_CROSS | PAD_CIRCLE) ? 20 : 1)) {
                    if (st[k].slot >= 0 || st[k].pane) printf("copy test f %u: pointer %d\n", frame, menu_test_point_file(st[k].slot, st[k].pane));
                    pad_force = st[k].btn;
                }
        }
#endif
#if defined(PACKS) && defined(TITLE_TEST)
        { /* the title: A+B, file 1 (new): Yes, then Start */
            pad_force = 0;
            if (frame >= 400 && frame < 420) pad_force = PAD_CROSS | PAD_CIRCLE;
            if (frame == 600) printf("title test f %u: pointer on file 1 %d\n", frame, menu_test_point_file(0, NULL));
            if (frame == 620) pad_force = PAD_CROSS;
            if (frame == 800) printf("title test f %u: pointer on BoxRight %d\n", frame, menu_test_point_file(0, "BoxRight"));
            if (frame == 820) pad_force = PAD_CROSS;
            if (frame == 1000) printf("title test f %u: pointer on Mii09 %d\n", frame, menu_test_point_file(0, "Mii09"));
            if (frame == 1020) pad_force = PAD_CROSS;
            if (frame == 1150) printf("title test f %u: pointer on BoxRight %d\n", frame, menu_test_point_file(0, "BoxRight"));
            if (frame == 1170) pad_force = PAD_CROSS;
            if (frame == 1450) printf("title test f %u: pointer on BoxStartButton %d\n", frame, menu_test_point_file(0, "BoxStartButton"));
            if (frame == 1470) pad_force = PAD_CROSS;
        }
#endif
#if defined(PACKS) && defined(NEWFILE_TEST)
        { /* the files from the card; XF=-DNEWFILE_SEED: none, file 1 seeded with progress, still loaded as after a quit
           * to the title, and saved (gamedata.c). Then file 2 (new): Yes, icon, Yes, Start -> a fresh game (the prologue);
           * XF=-DNEWFILE_SLOT=n: the n-th slot instead (0: file 1, Start -> its game; 2: file 4, new). It writes the card */
#ifndef NEWFILE_SLOT
#define NEWFILE_SLOT 1
#endif
            pad_force = 0;
            if (frame >= 400 && frame < 420) pad_force = PAD_CROSS | PAD_CIRCLE;
            if (frame == 600) printf("newfile test f %u: pointer on slot %d %d\n", frame, NEWFILE_SLOT, menu_test_point_file(NEWFILE_SLOT, NULL));
            if (frame == 620) pad_force = PAD_CROSS;
#if NEWFILE_SLOT != 0 /* (a new file there) */
            if (frame == 800) printf("newfile test f %u: pointer on BoxRight %d\n", frame, menu_test_point_file(NEWFILE_SLOT, "BoxRight"));
            if (frame == 820) pad_force = PAD_CROSS;
            if (frame == 1000) printf("newfile test f %u: pointer on Mii09 %d\n", frame, menu_test_point_file(NEWFILE_SLOT, "Mii09"));
            if (frame == 1020) pad_force = PAD_CROSS;
            if (frame == 1150) printf("newfile test f %u: pointer on BoxRight %d\n", frame, menu_test_point_file(NEWFILE_SLOT, "BoxRight"));
            if (frame == 1170) pad_force = PAD_CROSS;
#endif
            if (frame == 1450) printf("newfile test f %u: pointer on BoxStartButton %d\n", frame, menu_test_point_file(NEWFILE_SLOT, "BoxStartButton"));
            if (frame == 1470) pad_force = PAD_CROSS;
        }
#endif
#if defined(PACKS) && defined(MISS_TEST)
        if (frame == 300 || frame == 900) { /* a miss (the stage again, a life less), then a game over (the title) */
            printf("miss test f %u: lives %d\n", frame, game_lives());
            game_test_miss(frame == 900);
        }
        pad_force = 0;
        if (frame == 1340) printf("miss test f %u: save window, pointer on BoxRight %d\n", frame, menu_test_point("BoxRight"));
        if (frame == 1360 || frame == 1660) pad_force = PAD_CROSS; /* Yes, then the done window's key */
#endif
#if defined(PACKS) && defined(DOME_TEST)
        { /* the dome: R2 at the blue star, Good Egg opened (New), then chosen and confirmed, マリオ飛び出し, its scenario 1 */
            pad_force = 0;
            if (frame == 200) { /* the Gateway's grand star: Good Egg can open; the Butler's lectures heard */
                gd_set_star("HeavensDoorGalaxy", 1); gd_story_follow("天球儀レクチャー"); gd_story_follow("ギャラクシー移動レクチャー");
            }
            if (frame >= 300 && frame < 345) { int r = game_test_dome(0, 1); if (frame == 300) printf("dome test f %u: blue star %d\n", frame, r); }
            if (frame == 345) game_test_dome(0, 0);
            if (frame >= 600 && frame <= 620) { int st = game_test_dome(1, 0); if (frame == 600) printf("dome test f %u: galaxy 0 state %d\n", frame, st); }
            if (frame == 620) pad_force = PAD_CROSS;
            if (frame >= 820 && frame <= 840) { int st = game_test_dome(1, 0); if (frame == 820) printf("dome test f %u: galaxy 0 state %d\n", frame, st); }
            if (frame == 840) pad_force = PAD_CROSS;
            if (frame == 960) printf("dome test f %u: pointer on BoxStartButton %d\n", frame, menu_test_point_dome("BoxStartButton"));
            if (frame == 980) pad_force = PAD_CROSS;
            if (frame > 980 && frame < 1500 && frame % 30 == 0) /* マリオ飛び出し: its part * 1000 + step, then the scenario select */
                printf("dome test f %u: demo %d, selecting %d\n", frame, game_test_demo(-1), menu_scenario_selecting());
            if (frame == 1700) printf("dome test f %u: pointer on Star1 %d, selecting %d\n", frame, menu_test_point_scenario("Star1"), menu_scenario_selecting());
            if (frame == 1720) pad_force = PAD_CROSS;
        }
#endif
#if defined(PACKS) && defined(COMET_TEST)
        if (frame == 300) { /* a comet on Good Egg: its decided scenario and warning */
            gd_set_star("HeavensDoorGalaxy", 1); /* 13 stars: EventCometStarter (StarNum 13) */
            for (int k = 1; k <= 6; k++) { gd_set_star("EggStarGalaxy", k == 4 || k == 5 ? 7 : k); gd_set_star("HoneyBeeKingdomGalaxy", k); } gd_set_star("SurfingLv1Galaxy", 1); gd_set_star("FlipPanelExGalaxy", 1);
            int a = gd_flag_try_on("EventCometStarter");
            gd_comet_sync();
            printf("comet test: starter %d, Good Egg comet star %d\n", a, gd_comet_star("EggStarGalaxy"));
            menu_scenario_select("EggStarGalaxy", gd_comet_star("EggStarGalaxy"));
        }
#endif
#if defined(PACKS) && defined(SCENSEL_TEST)
        { /* the scenario select of EggStar with its first star: the pointer on Star2 (the new one), X: its scenario 2 */
            pad_force = 0;
            if (frame == 300) { gd_set_star("EggStarGalaxy", 1); menu_scenario_select("EggStarGalaxy", 0); }
            if (frame == 700) printf("scensel test f %u: pointer on Star2 %d\n", frame, menu_test_point_scenario("Star2"));
            if (frame == 730) { pad_force = PAD_CROSS; printf("scensel test f %u: X, selecting %d\n", frame, menu_scenario_selecting()); }
        }
#endif
#if defined(PACKS) && defined(PAUSE_TEST)
        { /* the pause menu: START, the pointer on its second button, X, then Yes in the confirm window */
            static const struct { u32 f; const char *pane; u32 btn; } st[] = {
                {500, NULL, PAD_START}, {620, "BoxButton2", 0}, {640, NULL, PAD_CROSS}, {900, "BoxRight", 0}, {920, NULL, PAD_CROSS},
                {1200, NULL, PAD_CROSS}, {1350, "BoxRight", 0}, {1370, NULL, PAD_CROSS}}; /* save and quit: Yes, saved, then the confirm's Yes */
            pad_force = 0;
            for (unsigned k = 0; k < sizeof st / sizeof st[0]; k++)
                if (frame == st[k].f) {
                    if (st[k].pane) printf("pause test f %u: pointer on %s %d\n", frame, st[k].pane, menu_test_point(st[k].pane));
                    pad_force = st[k].btn;
                    if (st[k].btn) printf("pause test f %u: button %x, paused %d\n", frame, st[k].btn, menu_paused());
                }
        }
#endif
#if defined(PACKS) && defined(LETTER_TEST)
        { /* a scenario with Luigi's letter (EggStarGalaxy/6): START, the pointer on the pause menu's LetterButton, X: the
           * letter and its IconAButton (PauseMenu::exeLuigiLetter); X once the icon waits: closed */
            pad_force = 0;
            if (frame == 900) pad_force = PAD_START;
            if (frame == 1020) printf("letter test f %u: paused %d, pointer on BoxButton3 %d\n", frame, menu_paused(), menu_test_point("BoxButton3"));
            if (frame == 1040) pad_force = PAD_CROSS;
            if (frame == 1300) pad_force = PAD_CROSS;
            if (frame >= 900 && frame < 1500 && frame % 20 == 0) printf("letter test f %u: paused %d button %d letter %d\n", frame, menu_paused(), menu_test_pml(), menu_test_letter());
        }
#endif
#if defined(PACKS) && defined(SCENE_TEST)
        if (frame == 500 || frame == 1000 || frame == 1500) { /* scenes in place, one after another */
            static const char *next[3] = {"EggStarGalaxy/2", "AstroGalaxy/1", "EggStarGalaxy/1"};
            printf("scene test f %u: stars %d lives %d mem %u KB, to %s\n", frame, game_stars(), game_lives(), mem_total() >> 10,
                   next[frame / 500 - 1]);
            game_request_scene(next[frame / 500 - 1]);
        }
        if (frame == 1990) printf("scene test end: %s mem %u KB\n", scen, mem_total() >> 10);
#endif
#ifdef PACKS
#ifdef HAZARD_TEST
        if (frame == 120)
            printf("hazard test: mine row %d\n", game_test_hazard(1));
        if (frame == 300 || frame == 400 || frame == 500) /* three pricks: the last one is a miss */
            printf("hazard test: plant row %d\n", game_test_hazard(2));
        if (frame == 600)
            printf("hazard test: electric rail %d\n", game_test_spark());
        if (frame == 850)
            printf("hazard test: at the start of RockCreator row %d\n", game_test_rockcreator());
        if (frame == 900)
            printf("hazard test: rock of RockCreator row %d, %d rolling\n", game_test_rock(), game_rocks_alive());
        if (frame == 800)
            printf("hazard test: fire bar row %d\n", game_test_firebar());
        if (frame == 700)
            printf("hazard test: black hole row %d\n", game_test_hole());
        if (frame % 20 == 0 && ((frame > 120 && frame < 200) || (frame > 300 && frame < 1000)))
            printf("hazard test f %u: hurt %d life %d misses %d\n", frame, game_hurt_timer(), game_life(),
                   game_misses());
#endif
#ifdef ITEM_TEST
        if (frame >= 120 && frame % 60 == 0) { /* every second Mario put on the next item, until none is left */
            static int done;
            int k = done ? 0 : game_test_item();
            if (!k && !done)
                printf("item test end: stars %d lives %d life %d, star pieces out %d\n", game_stars(), game_lives(),
                       game_life(), game_pieces_out());
            else if (k) printf("item test f %u: on item kind %d, star pieces out %d\n", frame, k, game_pieces_out());
            done = !k;
        }
#endif
#ifdef COLLAPSE_TEST
        static int crow = -1;
        if (frame == 120)
            printf("collapse test: Mario on plane row %d\n", crow = game_test_collapse());
        if (frame > 120 && frame % 30 == 0 && frame <= 420)
            printf("collapse test f %u: step %d\n", frame, game_test_collapse_step(crow));
#endif
#ifdef BOX_TEST
        if (frame == 120)
            printf("box test: Mario spins on box row %d\n", game_test_box());
        if (frame > 120 && frame <= 240 && frame % 10 == 0)
            printf("box test f %u: coins %d\n", frame, game_coins());
        if (frame == 300)
            printf("box test: Mario under item block row %d\n", game_test_block());
        if (frame == 360)
            printf("box test: after the block coins %d pieces out %d\n", game_coins(), game_pieces_out());
        if (frame == 240)
            printf("box test: coins %d\n", game_coins());
#endif
#ifdef RESTART_TEST
        if (frame == 120 || frame == 300) {
            char d[128];
            int no = game_test_restart(frame == 120 ? 0 : 1);
            game_debug(d, sizeof d);
            printf("restart test f %u: in RestartCube to MarioNo %d, %s\n", frame, no, d);
        }
        if (frame == 200 || frame == 380) {
            char d[128];
            game_test_restart(-1);
            game_debug(d, sizeof d);
            printf("restart test f %u: after a miss %s\n", frame, d);
        }
#endif
#ifdef SPOT_TEST
        if (frame >= 120 && frame <= 600 && frame % 40 == 0) /* breakables whose dead switch a StarPieceSpot waits for */
            printf("spot test f %u: spins on row %d, star pieces out %d\n", frame, game_test_spot(), game_pieces_out());
#endif
#ifdef CANNON_TEST
        if (frame >= 120 && frame <= 900 && frame % 30 == 0) /* held in its line of fire */
            printf("cannon test f %u: on row %d, life %d\n", frame, game_test_cannon(), game_life());
#endif
#ifdef SHOOT_TEST
        if (frame % 120 == 0 && frame <= 1800)
            printf("shoot test f %u: star pieces out %d\n", frame, game_pieces_out());
#endif
#ifdef IBUBBLE_TEST
        if (frame >= 400 && frame < 1100 && frame % 10 == 0) { /* off the first ItemBubble until 700, then into each */
            int row = game_test_ibubble(frame >= 700);
            if (frame % 50 == 0) printf("ibubble test f %u: row %d pieces out %d\n", frame, row, game_pieces_out());
        }
#endif
#ifdef EXPLORE_TEST /* videos: any galaxy played through the pad (game_demo_explore) */
        if (frame >= 150) game_demo_explore((int)frame);
#endif
#ifdef TEASER /* video demos: Mario through the pad only, every frame */
#if defined(TALK_TEST)
        if (frame >= 200 && frame < 3000) game_demo_walk1((int)frame);
#elif defined(FAT_TEST)
        if (frame >= 150) { int st = game_demo_fat((int)frame); if (frame % 30 == 0) { char dd[128]; game_debug(dd, sizeof dd); printf("fat demo f %u: %d %s\n", frame, st, dd); } }
#elif defined(VINE_TEST)
        if (frame >= 300) { int st = game_demo_vine((int)frame); if (frame % 20 == 0) { char dd[128]; game_debug(dd, sizeof dd); printf("vine demo f %u: %d %s\n", frame, st, dd); } }
#elif defined(DINO_TEST)
        if (frame >= 150) game_demo_dino((int)frame);
#elif defined(OTA_TEST)
        if (frame >= 150) game_demo_ota((int)frame);
#endif
#endif
#if defined(TALK_TEST) && !defined(TEASER)
        if (frame >= 200 && frame < 2400) game_test_talk((int)frame); /* walks to the first SignBoard and reads it */
        if (frame % 30 == 0 && frame < 2400) game_test_npc_turn((int)frame);
#endif
#ifdef SLIP_TEST
        if (frame >= 300 && frame < 2400) game_test_slip(); /* Mario left still on a steep slope */
#endif
#ifdef INVINC_TEST
        if (frame >= 400 && frame < 1700) { /* Mario takes the rainbow star, walks into a walker, is hit for nothing */
            int st = game_test_invinc();
            if (frame % 50 == 0) printf("invinc test f %u: invincible %d, life %d\n", frame, st >> 8, st & 255);
        }
#endif
#ifdef REPLICA_TEST
        if (frame >= 400 && frame < 700) { /* Mario on a CoinReplica: not taken until its SW_A is on (frame 550) */
            int st = game_test_replica(frame >= 550);
            if (frame % 25 == 0) printf("replica test f %u: coin %d, coins %d\n", frame, st, game_coins());
        }
#endif
#ifdef FOLLOW_TEST
        if (frame >= 400 && frame < 1000) { /* a StarPieceFollowGroup round Mario, then he takes its pieces one by one */
            int st = game_test_follow(frame >= 480 && frame % 10 == 0);
            if (frame % 20 == 0) printf("follow test f %u: out %d, Mario's pieces %d\n", frame, st >> 8, st & 255);
        }
#endif
#ifdef SEESAW_TEST
        if (frame >= 400 && frame < 1400) { /* Mario on a PhantomShipHandle's side, then a hip drop on its other side */
            int st = game_test_seesaw(frame >= 900);
            if (frame % 20 == 0) printf("seesaw test f %u: %d\n", frame, st);
        }
#endif
#ifdef PETARI_TEST
        if (frame >= 400 && frame < 2400) { /* a Petari: it comes, surfaces, a spin swoons it, a trample smashes it */
            int st = game_test_petari();
            if (frame % 25 == 0) printf("petari test f %u: %d\n", frame, st);
        }
#endif
#if defined(VINE_TEST) && !defined(TEASER)
        if (frame >= 400 && frame < 3000) { /* the Plant grown, Mario up it and off its top */
            int st = game_test_vine();
            if (frame % 25 == 0) printf("vine test f %u: %d\n", frame, st);
        }
#endif
#ifdef HHP_TEST
        if (frame >= 400 && frame < 2400) { /* Mario before the HammerHeadPackun, a spin in its Rest, a trample */
            int st = game_test_hhp();
            if (frame % 50 == 0) printf("hhp test f %u: %d\n", frame, st);
        }
#endif
#ifdef ROCK_TEST
        { /* the RockCreators out from the start, Mario held 3000 over the first one till 1800 (its zone streams in, the
           * rocks roll below him), then let go: every rolling rock stays near its rail's place (Good Egg row 463's rocks
           * drop ~1000 off its rail's end before they break: a fly-off goes on for thousands) */
            if (frame >= 120 && frame < 1800) game_test_rock_hold(0);
            static int worst;
            int pr = frame % 300 == 0;
            int far = game_test_rock_log(pr);
            if (far > worst) worst = far;
            if (pr) printf("rock test f %u: %d rolling, farthest from its rail %d (worst %d)\n", frame, game_rocks_alive(), far, worst);
            if (frame == 3000) printf("rock test end: worst %d from a rail: %s\n", worst, worst < 1500 ? "ok" : "FLEW OFF");
        }
#endif
#ifdef OROCK_TEST
        if (frame >= 400 && frame < 2400 && frame % 2 == 0) { /* Mario before the OtaRocks, spinning coconuts back */
            int st = game_test_orock();
            if (frame % 100 == 0) printf("orock test f %u: %d\n", frame, st);
        }
#endif
#ifdef KGUN_TEST
        if (frame >= 400 && frame < 1200 && frame % 10 == 0) { /* Mario held at the first KillerGunnerSingle's shell */
            int row = game_test_kgun();
            if (frame % 50 == 0) printf("kgun test f %u: row %d state %d life %d misses %d\n", frame, row, game_test_kgun_state(), game_life(), game_misses());
        }
#endif
#ifdef PLANT_TEST
        if (frame >= 400 && frame % 10 == 0) { /* after the ScenarioStarter flight (350) */
            static int k;
            int row = game_test_plant(k);
            if (row >= 0) k++;
            if (row != -1 || frame % 200 == 0)
                printf("plant test f %u: member %d of row %d, coins %d, pieces out %d\n", frame, k, row, game_coins(), game_pieces_out());
        }
#endif
#ifdef STEAM_TEST
        if (frame >= 120 && frame <= 900 && frame % 15 == 0) {
            int row = game_test_steam();
            if (frame % 60 == 0) printf("steam test f %u: in row %d, life %d\n", frame, row, game_life());
        }
#endif
#ifdef ASSEMBLY_TEST
        if (frame >= 120 && frame % 30 == 0) {
            int k = (int)(frame - 120) / 30, row = k < 40 ? game_test_assembly(k / 2) : -1;
            if (frame % 60 == 0) printf("assembly test f %u: block %d row %d, %d assembled\n", frame, k / 2, row, game_assembled());
        }
#endif
#ifdef JUMPSTAND_TEST
        {
            static unsigned at; /* the frame of the first drop, once its zone is in */
            if (!at && frame >= 120 && frame % 30 == 0 && game_test_jumpstand(0) >= 0) at = frame;
            if (at && frame == at + 300)
                printf("jumpstand test f %u: Mario dropped on row %d (hip drop)\n", frame, game_test_jumpstand(1));
            if (at && at == frame) printf("jumpstand test f %u: Mario dropped\n", frame);
        }
        if (frame > 120 && frame < 1500 && frame % 10 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("jumpstand test f %u: %s\n", frame, d);
        }
#endif
#ifdef WARPPOD_TEST
        if (frame == 120 || frame == 700)
            printf("warppod test f %u: Mario into row %d\n", frame, game_test_warppod());
        if (frame > 120 && frame < 1200 && frame % 20 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("warppod test f %u: %s\n", frame, d);
        }
#endif
#ifdef TSUKI_TEST
        if (frame >= 120 && frame % 30 == 0) {
            float c = 0;
            static int bound;
            int row = game_test_tsuki(!bound, &c);
            if (row >= 0) bound = 1;
            char d[128];
            game_debug(d, sizeof d);
            printf("tsuki test f %u: row %d coord %d, %s\n", frame, row, (int)c, d);
        }
#endif
#ifdef KIRAIRA_TEST
        if (frame >= 120 && frame % 60 == 0)
            printf("kiraira test f %u: Mario into row %d, life %d\n", frame, game_test_kiraira(), game_life());
#endif
#ifdef METEOR_TEST
        if (frame >= 120 && frame < 1500 && frame % 10 == 0) {
            int row = game_test_meteor();
            if (frame % 120 == 0) printf("meteor test f %u: in row %d's path, life %d" "\n", frame, row, game_life());
        }
#endif
#ifdef KILLER_TEST
        if (frame >= 200 && frame < 260) game_test_freeze(1);
        if (frame == 260) game_test_freeze(0);
        if (frame >= 120 && frame % 30 == 0) {
            static int bound;
            int row = bound ? -3 : game_test_killer();
            if (row >= 0) bound = 1;
            if (frame % 120 == 0) printf("killer test f %u: row %d, life %d" "\n", frame, row, game_life());
        }
#endif
#ifdef FLOW_TEST
        if (frame >= 120 && frame % 40 == 0 && frame <= 600)
            printf("flow test f %u: on piece %d, pieces %d\n", frame, game_test_flow(), game_pieces_out());
#endif
#ifdef CURSOR_TEST
        if (frame >= 120 && frame < 600) { /* a raster sweep of the screen: 12 rows, 40 frames each */
            int k = frame - 120, row = k / 40;
            float t = (k % 40) / 39.0f;
            game_set_cursor(row & 1 ? 620 - 600 * t : 20 + 600 * t, 20 + row * 37.0f);
        }
        if (frame % 60 == 0 && frame <= 660) printf("cursor test f %u: pieces %d\n", frame, game_starpieces());
#endif
#ifdef GCAP_TEST
        if (frame >= 120 && frame < 300) game_test_gcap(2); /* held over it while the camera settles */
        if (frame >= 300 && frame < 500) {
            int row = game_test_gcap(1);
            if (frame == 300) printf("gcap test: cursor on row %d\n", row);
        }
        if (frame == 500) printf("gcap test: R2 let go, phase %d\n", game_test_gcap(0));
        if (frame >= 300 && frame < 800 && frame % 20 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("gcap test f %u: %s\n", frame, d);
        }
#endif
#ifdef SHOOT2_TEST
        if (frame >= 120 && frame < 300) game_test_enemy(6, 0, 3); /* held 200 off a Takobo while the camera settles */
        if (frame == 300) printf("shoot2 test: shot at Takobo row %d\n", game_test_shoot(6));
        if (frame == 330) printf("shoot2 test: stomp it row %d\n", game_test_enemy(6, 0, 0));
        if (frame == 360) printf("shoot2 test: %d Takobo alive\n", game_kind_alive(6));
#endif
#ifdef TSPOT_TEST
        if (frame >= 120 && frame < 300) game_test_tspot_near(); /* the camera settles */
        if (frame == 300 || frame == 360) printf("tspot test f %u: shot at row %d, coins %d\n", frame, game_test_tspot(), game_coins());
        if (frame == 340 || frame == 400) printf("tspot test f %u: coins %d\n", frame, game_coins());
#endif
#ifdef PROM_TEST
        if (frame >= 120 && frame < 1500 && frame % 10 == 0) {
            int row = game_test_prom();
            if (frame % 120 == 0) printf("prom test f %u: row %d, life %d\n", frame, row, game_life());
        }
#endif
#ifdef STONE_TEST
        if (frame >= 120 && frame < 600 && frame % 60 < 30) game_test_stone(0); /* its zone streams in, the ring drops */
        if (frame >= 120 && frame < 600 && frame % 60 == 30) printf("stone test f %u: spin at row %d, pieces %d\n", frame, game_test_stone(1), game_pieces_out());
#endif
#ifdef BEEFLOWER_TEST
        if (frame >= 120 && frame % 60 == 0 && frame <= 900) {
            char d[128];
            game_debug(d, sizeof d);
            printf("beeflower test f %u: dropped on row %d, %s\n", frame, game_test_beeflower(), d);
        }
#endif
#ifdef TICORAIL_TEST
        if (frame >= 120 && frame % 60 == 0 && frame <= 1500) {
            float c = 0;
            int row = game_test_ticorail(&c);
            printf("ticorail test f %u: row %d coord %d\n", frame, row, (int)c);
        }
#endif
#ifdef JBRANCH_TEST
        {
            static unsigned at;
            if (!at && frame >= 120 && frame % 30 == 0 && game_test_jbranch(0) >= 0) at = frame;
            if (at && frame > at && frame < at + 120 && frame % 10 == 0) {
                char d[128];
                game_debug(d, sizeof d);
                printf("jbranch test f %u: swinging row %d, %s\n", frame, game_test_jbranch(1), d);
            }
        }
#endif
#ifdef GIVER_TEST
        if (frame >= 120 && frame % 40 == 0 && frame <= 1000)
            printf("giver test f %u: %s row %d, coins %d, pieces %d\n", frame, frame % 80 ? "leaves" : "pompon",
                   game_test_giver(frame % 80 ? 1 : 2), game_coins(), game_pieces_out());
#endif
#if defined(PACKS) && defined(MORPH_TEST)
        game_test_morph((int)frame);
        if (frame % 30 == 0) printf("morph perf f %u: logic %u draw %u us, tris %u\n", frame, t_logic / 295, t_draw / 295, render_tris_drawn());
#endif
#ifdef BEE_TEST
        { /* Bee Mario jumps (150), A held from 190 to 400 (flies, runs out), let go */
            if (frame == 150) { game_test_bee(0); game_test_jump(); }
            if (frame == 190) game_test_bee(1);
            if (frame == 400) game_test_bee(2);
            if (frame >= 150 && frame < 600 && frame % 10 == 0) {
                char dd[128];
                game_debug(dd, sizeof dd);
                printf("bee test f %u: flight %d %s\n", frame, game_bee_state(), dd);
            }
        }
#endif
#ifdef FIRE_TEST
#ifndef TEST_FIRE_KIND
#define TEST_FIRE_KIND 0 /* Kuribo */
#endif
        { /* Fire Mario by a walker (150), spins every 40 frames */
#ifdef FIRE_AT_CANDLE
            if (frame >= 150 && frame < 300 && frame % 30 == 0) printf("fire test f %u: %d\n", frame, game_test_fire(0));
            if (frame == 300) game_test_fire(2);
            if (frame >= 320 && frame < 600 && frame % 40 == 0) game_test_fire(1);
#else
            if (frame == 150) { game_test_fire(0); game_test_enemy(TEST_FIRE_KIND, 0, 3); }
            if (frame > 150 && frame < 600 && frame % 40 == 0) game_test_fire(1);
#endif
            if (frame >= 150 && frame < 600 && frame % 10 == 0) printf("fire test f %u: balls %d\n", frame, game_fire_state());
        }
#endif
#if defined(FAT_TEST) && !defined(TEASER)
        if (frame >= 150 && frame % 6 == 0) { /* star pieces shot at the TicoFat until it is full */
            int st = game_test_fat();
            if (frame % 120 == 0) printf("fat test f %u: %d\n", frame, st);
        }
#endif
#ifdef WALKER_TEST
#ifdef TOWER_TEST
        { extern void game_test_tower(unsigned); game_test_tower(frame); }
#elif defined HANG_TEST
        { extern void game_test_hang(unsigned); game_test_hang(frame); }
#elif defined ICE_TEST
        { extern void game_test_ice(unsigned); game_test_ice(frame); }
#else
        if (frame == 150 || frame == 450 || frame == 750 || frame == 1050) { /* Mario by a Kuribo, a Takobo, a Karikari, a Butterfly */
            static const char *cls[4] = {"Kuribo", "Takobo", "Karikari", "Butterfly"};
            printf("walker test f %u: %s row %d\n", frame, cls[frame / 300], game_test_walker(cls[frame / 300], 0));
        }
#endif
#endif
#ifdef ROUTE_TEST
        if (frame >= 150) { /* the scenario played through: launch stars to its goal, its action, its star */
            static int phase, last;
            int n = scen[strlen(scen) - 1] - '0';
            const char *goal = n == 2 ? "TicoFat" : n == 3 ? "OtaKing" : "DinoPackun";
            int st = 0;
            if (n == 2) game_test_fat_nodrop();
            if (game_stars()) phase = 10; /* done */
            if (phase == 0) {
                st = game_test_route(goal);
                if (st == 0) { phase = 1; printf("route: at the %s, f %u\n", goal, frame); }
                else if (st < 0) { phase = 9; printf("route: stuck on the way to the %s\n", goal); }
            } else if (phase == 1 && frame % (n == 2 ? 6 : n == 3 ? 1 : 20) == 0) {
                st = n == 2 ? game_test_fat() : n == 3 ? game_test_ota() : game_test_dino();
                if (n == 2 && (st == -1 || st == -3 || st == -4)) { phase = 3; printf("route: fed, f %u\n", frame); }
                else if (n != 2 && (n == 3 ? st / 10 == 5 : st / 100 == 6)) { phase = 2; printf("route: boss down, f %u\n", frame); }
            } else if (phase == 2 && frame % 20 == 0) {
                st = game_test_cage(1);
            } else if (phase == 3) {
                st = game_test_route("StarEggRoadEggPlanetD");
                if (st == 0) phase = 4;
            } else if (phase == 4 && frame % 20 == 0) {
                st = game_test_star_near();
            }
            if (game_stars() != last) { last = game_stars(); printf("route: star got, f %u\n", frame); }
            if (frame % 200 == 0) {
                char dd[128];
                game_debug(dd, sizeof dd);
                printf("route f %u: phase %d st %d stars %d %s\n", frame, phase, st, game_stars(), dd);
            }
        }
#endif
#ifdef PURPLE_TEST
        if (frame >= 150 && frame % 4 == 0) { /* Mario through the purple coins, then onto the 100-coin star */
            int st = game_test_purple();
            if (frame % 100 == 0) printf("purple test f %u: %d, stars %d\n", frame, st, game_stars());
        }
#endif
#if defined(OTA_TEST) && !defined(TEASER)
        if (frame >= 150) { /* Mario before OtaKing, spinning its coconuts back, until its star is out */
            int st = game_test_ota();
            if (st / 10 == 5) st = 5000 + game_test_cage(1); /* on its star */
            if (frame % 100 == 0) printf("ota test f %u: %d, stars %d\n", frame, st, game_stars());
        }
#endif
#if defined(DINO_TEST) && !defined(TEASER)
        if (frame >= 150 && frame % 20 == 0) { /* Mario at DinoPackun's tail ball, spinning, until its star is out */
            int st = game_test_dino();
            if (st / 100 == 6) st = 6000 + game_test_cage(1); /* on its star */
            if (frame % 100 == 0 || st >= 6000) printf("dino test f %u: %d, stars %d\n", frame, st, game_stars());
        }
#endif
#ifdef BUBBLE_TEST
        { /* Mario held 300 over an AirBubbleGenerator with little oxygen: a bubble fills it */
            if (frame >= 150 && frame < 600 && frame % 30 == 0) {
                int row = game_test_bubble();
                printf("bubble test f %u: row %d, %d\n", frame, row, game_swim_state());
            }
        }
#endif
#ifdef SWIM_TEST
        { /* Mario dropped in a water area (150): floats up to the surface, stays */
            { extern void game_test_swim_input(int); game_test_swim_input((int)frame); }
            if (frame >= 150 && frame < 1000 && frame % 20 == 0) {
                char dd[128];
                game_debug(dd, sizeof dd);
                printf("swim test f %u: %d %s\n", frame, game_swim_state(), dd);
            }
        }
#endif
#ifdef STRAY_TEST
        if (frame >= 150 && frame < 900 && frame % 40 == 0) /* Mario onto the stray Lumas one after another */
            printf("stray test f %u: row %d, stars %d\n", frame, game_test_stray(), game_stars());
#endif
#ifdef FOUNTAIN_TEST
        if (frame >= 150 && frame < 900 && frame % 60 == 0) { /* Mario back on a fountain's base every second */
            char dd[128];
            game_debug(dd, sizeof dd);
            printf("fountain test f %u: row %d %s\n", frame, game_test_fountain(), dd);
        }
#endif
#ifdef CANDLE_TEST
        if (frame >= 150 && frame < 600 && frame % 40 == 0) /* into a flame, then a spin by the next, in turn */
            printf("candle test f %u: row %d, life %d, coins %d\n", frame, game_test_candle(frame % 80 == 0), game_life(), game_coins());
#endif
#ifdef NPC_TEST
        { /* Mario beside each turning NPC in turn (300 frames each): their turn, tilt and action */
            if (frame >= 150 && frame % 300 == 150) printf("npc test f %u: visit row %d\n", frame, game_test_npc_visit((int)(frame - 150) / 300));
            if (frame >= 150 && frame % 60 == 0) game_test_npc_turn((int)frame);
        }
#endif
#ifdef HANACHAN_TEST
    {   /* frames from Mario free (the flight in over) + 150 */
        static u32 f0;
        u32 mf = frame;
        if (!f0 && mf > 30 && game_test_free()) f0 = mf;
        u32 frame = f0 ? mf - f0 + 150 : 0;
        if (frame == 200 || frame == 860) printf("hanachan test f %u: trample row %d\n", frame, game_test_hanachan(0));
        if (frame == 760) printf("hanachan test f %u: spin row %d\n", frame, game_test_hanachan(1));
        if (frame >= 200 && frame <= 1100 && frame % 20 == 0) printf("hanachan test f %u: nerve %d\n", frame, game_hanachan_state());
    }
#endif
#ifdef STRSPIDER_TEST
    {
        static u32 f0;
        u32 mf = frame;
        if (!f0 && mf > 30 && game_test_free()) f0 = mf;
        u32 frame = f0 ? mf - f0 + 150 : 0;
        { /* under the first spider (200, held to 400: it comes down), spins (400, 440: Spinned, Blow); the next one trampled (600 under, 800 onto) */
            if (frame == 200 || frame == 600) { printf("strspider test f %u: under row %d\n", frame, game_test_strspider(0)); game_test_hold(1); }
            if ((frame > 200 && frame < 400) || (frame > 600 && frame < 800)) game_test_hold(0);
            if (frame == 400 || frame == 440) printf("strspider test f %u: spin row %d\n", frame, game_test_strspider(1));
            if (frame == 800) printf("strspider test f %u: trample row %d\n", frame, game_test_strspider(2));
            if (frame >= 200 && frame <= 900 && frame % 20 == 0) game_strspider_state();
            game_test_unclip(); /* the spider above Mario is off the view */
        }
    }
#endif
#ifdef BEGOMAN_TEST
    {
        static u32 f0;
        u32 mf = frame;
        if (!f0 && mf > 30 && game_test_free()) f0 = mf;
        u32 frame = f0 ? mf - f0 + 150 : 0;
        /* 900 before the first spike (200: it comes for him), a spin by it (600: blown), a trample (900: Mario hurt) */
        if (frame >= 100 && frame < 200 && frame % 20 == 0) game_test_begoman(0); /* its zone in */
        if (frame == 200) printf("begoman test f %u: before row %d\n", frame, game_test_begoman(0));
        if (frame > 600 && frame < 640) game_test_spin_now(); /* (a flip from its last hit may stop the first) */
        if (frame == 600) printf("begoman test f %u: spin row %d\n", frame, game_test_begoman(1));
        if (frame == 900) printf("begoman test f %u: trample row %d\n", frame, game_test_begoman(2));
        if (frame >= 200 && frame <= 1100 && frame % 20 == 0) { printf("begoman test f %u: life %d\n", frame, game_life()); game_begoman_state(); }
    }
#endif
#ifdef SPIDERCOIN_TEST
        if (frame >= 150 && frame <= 900 && frame % 30 == 0) printf("spidercoin test f %u: row %d, coins %d\n", frame, game_test_spidercoin(), game_coins());
#endif
#ifdef SPIDERBLOCK_TEST
    {
        static u32 f0;
        u32 mf = frame;
        if (!f0 && mf > 30 && game_test_free()) f0 = mf;
        u32 frame = f0 ? mf - f0 + 150 : 0;
        if (frame >= 150 && frame < 500 && frame % 10 == 0) game_test_spiderblock(); /* its zone in */
        if (frame == 500) printf("spiderblock test: above row %d\n", game_test_spiderblock()); /* a hip drop onto the first SpiderMapBlock */
        if (frame > 500 && frame <= 700 && frame % 10 == 0) { char d[160]; game_debug(d, sizeof d); printf("spiderblock test f %u: hip %d coins %d %s\n", frame, game_hip_state(), game_coins(), d); }
    }
#endif
#ifdef STATUE_TEST
        if (frame >= 200 && frame % 100 == 0 && frame <= 600) printf("statue test f %u: row %d, coins %d\n", frame, game_test_statue(), game_coins());
#endif
#ifdef TORCH_TEST
        if (frame >= 150 && frame < 1350) { /* by a torch with an item, held 150 frames (its zone's collision streams in), a spin, then onto its item */
            int t = (int)(frame - 150) % 300, row = -9;
            if (t == 0) { row = game_test_torch(3); game_test_hold(1); }
            else if (t < 150) game_test_hold(0);
            else if (t == 150) row = game_test_torch(1);
            else if (t > 165) game_test_torch(2); /* each frame: Mario kept on its item (a coin falls where the house is low) */
            if (row != -9 || frame % 40 == 0)
                printf("torch test f %u: row %d, life %d, coins %d, star pieces %d (out %d)\n", frame, row, game_life(), game_coins(),
                       game_test_spc_n(), game_pieces_out());
        }
#endif
#ifdef SCREW_TEST
        { /* Mario dropped on a screw switch (150), a spin once he stands on it (180) */
            if (frame == 150) printf("screw test f %u: row %d\n", frame, game_test_screw());
            if (frame == 180) game_test_spin_now();
            if (frame >= 150 && frame < 400 && frame % 10 == 0) printf("screw test f %u: state %d\n", frame, game_screw_state());
        }
#endif
#ifdef JET_TEST
        { /* Mario onto a JetTurtle (150), walking with it, a spin throws it (250) */
            if (frame == 150) printf("jet test f %u: row %d\n", frame, game_test_jet(0));
            if (frame == 250) game_test_jet(1);
            if (frame >= 150 && frame < 700 && frame % 20 == 0) printf("jet test f %u: phase %d\n", frame, game_jet_state());
        }
#endif
#ifdef KEY_TEST
        if (frame >= 150 && frame < 600 && frame % 30 == 0) /* Mario onto the keys shown, one after another */
            printf("key test f %u: row %d, taken %d\n", frame, game_test_key(), game_key_taken());
#endif
#ifdef NOTE_TEST
        { /* the first note fairy out (150), Mario onto its notes one after another (from 300) */
            if (frame == 150) printf("note test f %u: row %d\n", frame, game_test_note(0));
            if (frame >= 300 && frame < 2400 && frame % 5 == 0) {
                int left = game_test_note(1);
                if (frame % 30 == 0) printf("note test f %u: %d left\n", frame, left);
            }
        }
#endif
#ifdef POIHANA_TEST
        { /* Mario in front of a Poihana (150): it chases, catches and throws him; a spin by it (600) */
            static int at;
            if ((frame >= 150 && !at && (at = game_test_poihana(0) >= 0 ? frame : 0)) || frame == 600) printf("poihana test f %u: row %d\n", frame, game_test_poihana(frame == 600));
            if (frame >= 150 && frame < 800 && frame % 10 == 0) {
                char dd[128];
                game_debug(dd, sizeof dd);
                printf("poihana test f %u: phase %d %s\n", frame, game_poihana_state(), dd);
            }
        }
#endif
#ifdef UNIZO_TEST
        { /* Mario 800 off a Unizo (150): it chases and hits him */
            if (frame == 150) printf("unizo test f %u: row %d\n", frame, game_test_unizo());
            if (frame >= 150 && frame < 700 && frame % 10 == 0) {
                int dist = 0, st = game_unizo_state(&dist);
                printf("unizo test f %u: phase %d dist %d life %d\n", frame, st, dist, game_life());
            }
        }
#endif
#ifdef STINK_TEST
        { /* Mario in front of a stink bug (150, 300), then a hip drop on it (500) */
            if (frame == 150 || frame == 300 || frame == 500)
                printf("stink test f %u: row %d\n", frame, game_test_stink(frame == 500));
            if (frame >= 150 && frame < 600 && frame % 10 == 0) {
                int dist = 0, st = game_stink_state(&dist);
                printf("stink test f %u: phase %d dist %d life %d\n", frame, st, dist, game_life());
            }
        }
#endif
#ifdef WATERPLANT_TEST
        if (frame >= 200 && frame % 60 == 20 && frame < 600) printf("waterplant test f %u: %d plants\n", frame, game_test_waterplant());
#endif
#ifdef FLUFF_TEST
        { /* a spin by a Fluff (150), Mario jumps into it (190), spins up (230, 330); logged every 10 frames */
            int r = -9;
            if (frame < 150 && frame % 30 == 0) r = game_test_fluff(-1); /* to its zone */
            if (frame == 150) r = game_test_fluff(0);
            else if (frame == 190) r = game_test_fluff(1);
            if (frame == 230 || frame == 330) game_test_fluff(3);
            if (frame >= 150 && frame < 600 && frame % 10 == 0) {
                char d[128];
                game_debug(d, sizeof d);
                printf("fluff test f %u: %d spins %d %s\n", frame, r, game_test_fluff(2), d);
            }
        }
#endif
#ifdef COCOON_TEST
        { /* Mario onto a cocoon (120), aim (200), drag 150 px down (210-290), let go (300); logged every 10 frames */
            int ph = -1;
            if (frame >= 120 && frame < 200 && frame % 20 == 0 && game_test_cocoon(4, 0) == 0) ph = game_test_cocoon(0, 0);
            else if (frame >= 200 && frame < 210) ph = game_test_cocoon(1, 0);
            else if (frame >= 210 && frame < 300) ph = game_test_cocoon(2, 150.0f * (frame - 210) / 90.0f);
            else if (frame == 300) ph = game_test_cocoon(3, 0);
            if (frame >= 120 && frame < 500 && frame % 10 == 0) {
                char d[128];
                game_debug(d, sizeof d);
                printf("cocoon test f %u: %d %s\n", frame, ph, d);
            }
        }
#endif
#ifdef COCO_TEST
        { /* a spin at 150, a kick at 600 */
            if (frame == 150 || frame == 600 || (frame < 150 && frame % 30 == 0 && game_test_coco(2) == -2))
                printf("coco test f %u: row %d\n", frame, game_test_coco(frame == 600));
            if (frame >= 150 && frame < 1000 && frame % 10 == 0) {
                int dist = 0, st = game_coco_state(&dist);
                printf("coco test f %u: phase %d dist %d\n", frame, st, dist);
            }
        }
#endif
#ifdef PUNCH_TEST
        { /* a spin at 150, then at 600 a hip drop; the head logged every 10 frames */
            if (frame == 150 || frame == 600 || (frame < 150 && frame % 30 == 0 && game_test_punch(2) == -2))
                printf("punch test f %u: row %d\n", frame, game_test_punch(frame == 600));
            if (frame >= 150 && frame < 900 && frame % 10 == 0) {
                int dist = 0, st = game_punch_state(&dist);
                printf("punch test f %u: phase %d head %d life %d\n", frame, st, dist, game_life());
            }
        }
#endif
#ifdef BIRI_TEST
        if (frame >= 120 && frame < 1200 && frame % 20 == 0) {
            int row = game_test_biri();
            if (frame % 120 == 0) printf("biri test f %u: row %d, life %d\n", frame, row, game_life());
        }
#endif
#ifdef GEYSER_TEST
        if (frame >= 120 && frame <= 1200 && frame % 15 == 0) {
            int row = game_test_geyser();
            if (frame % 60 == 0) printf("geyser test f %u: in row %d, life %d\n", frame, row, game_life());
        }
#endif
#ifdef CAGE_TEST
        if (frame == 120 || frame == 300 || frame == 380) /* the first: its zone streams in */
            printf("cage test f %u: Mario spins on row %d\n", frame, game_test_cage(0));
        if (frame == 440 || frame == 740)
            printf("cage test f %u: on its star, state %d, stars %d\n", frame, game_test_cage(1), game_stars());
#endif
#ifdef SPIN_TEST
        if (frame == 120)
            printf("spin test: spin driver %d\n", game_test_spindriver(1));
        if (frame > 120 && frame <= 600 && frame % 15 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("spin test f %u: launch %d %s\n", frame, game_launch_state(), d);
        }
#endif
#if defined(ROTOR_TEST) || defined(RAIL_TEST)
        {
            static int row = -1;
#ifdef RAIL_TEST
            if (frame == 120) printf("rail test: on row %d\n", row = game_test_rotor(1));
#else
            if (frame == 120) printf("rotor test: on row %d\n", row = game_test_rotor(0));
#endif
            if (frame > 120 && frame <= 600 && frame % 30 == 0) {
                char d[128];
                game_debug(d, sizeof d);
                printf("rotor test f %u: angle %d coord %d %s\n", frame, (int)game_rotor_angle(row),
                       (int)game_rail_coord(row), d);
            }
        }
#endif
#ifdef FLIP_TEST
        if (frame >= 120 && frame % 40 == 0) { /* every 40 frames Mario on the next panel */
            static int k, done;
            if (!done && k) {
                char d[128];
                game_debug(d, sizeof d);
                printf("flip test f %u: panel %d, %d on their back, %s\n", frame, k - 1, game_flips_back(), d);
            }
            int row = done ? -1 : game_test_flip(k++);
            if (row < 0 && done == 0) { /* back to panel 2 too: Mario's start point flipped it, his visit flipped it back */
                printf("flip test end: %d panels\n", k - 1);
                game_test_flip(2);
                done = 1;
            } else if (done == 1) {
                printf("flip test after panel 2 again: %d on their back\n", game_flips_back());
                done = 2;
            } else if (done >= 2 && ++done == 8) /* its power star out and in place (210 frames): Mario on it */
                printf("flip test: on item %d\n", game_test_item());
        }
#endif
#ifdef TAKOBO_TEST
        if (frame >= 120 && frame < 300 && frame % 10 == 0) { /* held 200 off: it stops and punches */
            int row = game_test_enemy(6, 0, 3);
            if (frame % 30 == 0) printf("takobo test f %u: by row %d, life %d\n", frame, row, game_life());
        }
        if (frame == 400) printf("takobo test: stomp row %d\n", game_test_enemy(6, 0, 0));
        if (frame == 520) printf("takobo test: spin by row %d\n", game_test_enemy(6, 0, 1));
        if (frame == 640) printf("takobo test: %d alive, life %d\n", game_kind_alive(6), game_life());
#endif
#ifdef KARIKARI_TEST
        if (frame == 500) printf("karikari test: R2 on row %d\n", game_test_kari_point(1));
        if (frame == 501) game_test_kari_point(0);
        if (frame == 120 || frame == 160 || frame == 200) printf("karikari test f %u: by row %d" "\n", frame, game_test_enemy(8, 0, 3));
        if (frame % 60 == 0 && frame <= 900) printf("karikari test f %u: life %d" "\n", frame, game_life());
        if (frame == 700) printf("karikari test: hip drop by row %d" "\n", game_test_enemy(8, 0, 5));
#endif
#ifdef MKOOPA_TEST
        if (frame >= 120 && frame < 400 && frame % 10 == 0) { /* held 200 off: it hops, closes in, fires */
            int row = game_test_enemy(7, 0, 3);
            if (frame % 30 == 0) printf("mkoopa test f %u: by row %d, life %d" "\n", frame, row, game_life());
        }
        if (frame == 500) printf("mkoopa test: stomp row %d" "\n", game_test_enemy(7, 0, 0));
        if (frame == 800) printf("mkoopa test: hip drop row %d\n", game_test_enemy(7, 0, 4));
        if (frame == 700 || frame == 900) printf("mkoopa test f %u: %d alive, life %d\n", frame, game_kind_alive(7), game_life());
#endif
#ifdef KMINI_TEST
        if (frame == 120) printf("kmini test: stomp row %d\n", game_test_enemy(9, 0, 0));
        if (frame == 240) printf("kmini test: spin by row %d\n", game_test_enemy(9, 0, 1));
        if (frame == 360) printf("kmini test: %d alive\n", game_kind_alive(9));
#endif
#ifdef KARON_TEST
        if (frame == 120) printf("karon test: stomp row %d\n", game_test_enemy(10, 0, 0));
        if (frame >= 200 && frame < 600 && frame % 60 == 0) printf("karon test f %u: %d up, life %d\n", frame, game_kind_alive(10), game_life());
#endif
#ifdef TERESA_TEST
        if (frame == 120) { game_test_enemy(11, 0, 3); game_test_hold(1); } /* 200 off a Boo, his back to it */
        if (frame > 120 && frame < 400) game_test_hold(0); /* held still: it comes to him */
        if (frame % 30 == 0 && frame >= 120 && frame <= 600) { char w[96]; game_walker_state(11, w, sizeof w); printf("teresa test f %u: life %d, %s\n", frame, game_life(), w); }
        if (frame == 450) printf("teresa test: spin by row %d\n", game_test_enemy(11, 0, 1));
#endif
#ifdef BUTTERFLY_TEST
        if (frame >= 120 && frame < 300) game_test_enemy(12, 0, 3); /* by a butterfly while the camera settles */
        if (frame == 300 || frame == 360) printf("butterfly test f %u: pointer on row %d, pieces %d\n", frame, game_test_point_walker(12), game_pieces_out());
        if (frame == 420) printf("butterfly test: pieces %d\n", game_pieces_out());
#endif
#ifdef ENEMY_TEST
        if (frame == 120) printf("enemy test: stomp TakoHei row %d\n", game_test_enemy(1, 0, 0));
        if (frame == 240) printf("enemy test: spin by PackunPetit row %d\n", game_test_enemy(2, 0, 1));
        if (frame == 360) printf("enemy test: stand by PackunPetit row %d\n", game_test_enemy(2, 0, 2));
        if (frame == 600) printf("enemy test: stomp KoteBug row %d\n", game_test_enemy(3, 0, 0));
        if (frame == 780) printf("enemy test: stomp it again row %d\n", game_test_enemy(3, 0, 0));
        if (frame == 900) printf("enemy test: stand by JumpSpider row %d\n", game_test_enemy(4, 0, 2));
        if (frame == 1020) printf("enemy test: spin by JumpSpider row %d\n", game_test_enemy(4, 0, 1));
        if (frame % 60 == 0 && frame >= 180 && frame <= 1140)
            printf("enemy test f %u: takohei %d packun %d kotebug %d spider %d life %d coins %d\n", frame,
                   game_kind_alive(1), game_kind_alive(2), game_kind_alive(3), game_kind_alive(4), game_life(), game_coins());
#endif
#ifdef HIP_TEST
        if (frame == 120)
            printf("hip test: above row %d\n", game_test_hip());
        if (frame > 120 && frame <= 420 && frame % 10 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("hip test f %u: hip %d coins %d pieces %d %s\n", frame, game_hip_state(), game_coins(),
                   game_starpieces(), d);
        }
#endif
#ifdef PIPE_TEST
        if (frame == 120)
            printf("pipe test: on pipe %d\n", game_test_pipe());
        if (frame > 120 && frame <= 300 && frame % 15 == 0) {
            char d[128];
            game_debug(d, sizeof d);
            printf("pipe test f %u: %s\n", frame, d);
        }
#endif
#ifdef ZONE_TEST
        if (frame == 120)
            printf("zone test: launch star %d\n", game_test_spindriver(0));
        { /* per 60 frames: frames longer than 20 ms (missed vsync) and the worst logic / draw / GS of the window */
            static u32 prev, slow, wl, wd, wg;
            if (prev && t0 - prev > (u32)(20000 * EE_MHZ)) slow++;
            prev = t0;
            if (t_logic > wl) wl = t_logic;
            if (t_draw > wd) wd = t_draw;
            if (t_gs > wg) wg = t_gs;
            if (frame % 60 == 0 && frame <= 900) {
                char d[128];
                game_debug(d, sizeof d);
                printf("zone test f %u: slow %u, worst logic %u draw %u gs %u us; %s\n", frame, slow,
                       (u32)(wl / EE_MHZ), (u32)(wd / EE_MHZ), (u32)(wg / EE_MHZ), d);
                slow = wl = wd = wg = 0;
            }
        }
#endif
#ifdef COVER_TEST
        /* coverage run (tools/coverage.ps1): a launch star at frame 400 (after the ScenarioStarter flight) if there is
         * one, then one line at 1200 */
        if (frame == 400)
            game_test_spindriver(0);
        {
            static u32 prev, slow;
            if (frame > 120 && prev && t0 - prev > (u32)(20000 * EE_MHZ)) slow++;
            prev = t0;
            if (frame == 1200)
                printf("cover: %s ok, slow %u, worst %u us, runtime %u KB, lives %d, misses %d\n", scen, slow,
                       (u32)(worst / EE_MHZ), mem_total() >> 10, game_lives(), game_misses());
        }
#endif
        stream_update(&scene);
        game_bgm_tick();
        audio_update();
#endif
#ifdef SND_TEST
        se_test(frame);
#endif
        {
            V3 eye, watch, up;
            float fovy;
            camera_pose(&eye, &watch, &up, &fovy);
            V3 r = vnorm(vcross(vsub(watch, eye), up));
            float e[3] = {eye.x, eye.y, eye.z}, rr[3] = {r.x, r.y, r.z};
            se_listener(e, rr);
        }
        me_update();
        se_update(steps);
        se_flush();
#if defined(ROUTE_TEST) || defined(SND_TEST) || defined(WALKER_TEST) || defined(FAT_TEST)
        if (frame % 5 == 0) {
            int ns, nc, nv;
            se_stats(&ns, &nc, &nv);
            if (ns) printf("se: frame %d sounds %d chans %d voices %d\n", frame, ns, nc, nv);
        }
#endif
        u32 t1 = cycles();
        anim_rate = (float)steps; game_set_draw_steps(steps); /* draw-time clips after `steps` logic steps */
        if (menu_paused()) { anim_rate = 0; game_set_draw_steps(0); } /* the world held under the pause menu */
        { /* GamePauseSequence: onPauseBeginAllRumble / onPauseEndAllRumble (both stop the rumble) */
            static int was_paused;
            if (menu_paused() != was_paused) { was_paused = menu_paused(); pad_rumble_stop(); }
        }
        game_draw();
        audio_feed();
        anim_rate = 1.0f;
#if defined(PACKS) && defined(LYT_TEST)
        { /* the pause menu's layout over the game: Appear and the buttons' ButtonAppear, then their Wait */
            static LytInst *pm;
            if (frame == 200) {
                pm = lyt_new("PauseMenu");
                printf("lyt test: PauseMenu %s, appear %d buttons %d\n", pm ? "found" : "missing", lyt_start(pm, "Appear", 0),
                       lyt_start(pm, "ButtonAppear", 1));
            }
            if (pm && frame == 320) printf("lyt test f %u: appear stopped %d frame %d\n", frame, lyt_stopped(pm, 0), (int)lyt_frame(pm, 0));
            if (pm) { for (int k = 0; k < steps; k++) lyt_update(pm); lyt_draw(pm); }
        }
#endif
        u32 t2 = cycles();
        t_logic = t1 - t0;
        t_draw = t2 - t1;
#ifdef ROUTE_TEST
        { /* the EE's time over each 200 frames of the route (the draw without render_begin's flip / VIF / clear wait),
           * for the heavy places a bench path does not cross */
            extern u32 render_begin_wait;
            static u64 rl, rd;
            static u32 rw;
            u32 d = t_draw - render_begin_wait;
            rl += t_logic; rd += d;
            if (t_logic + d > rw) rw = t_logic + d;
            if (frame % 200 == 0) {
                extern u32 rebind_cycles, rebinds;
                printf("route perf f %u: logic %u draw %u us, worst %u, %u rebinds %u us\n", frame, (u32)(rl / 200 / EE_MHZ), (u32)(rd / 200 / EE_MHZ), (u32)(rw / EE_MHZ), rebinds, (u32)(rebind_cycles / EE_MHZ));
                rebinds = rebind_cycles = 0;
                rl = rd = 0; rw = 0;
            }
        }
#endif
        if (t2 - t0 > worst && frame > 60) worst = t2 - t0;
#ifdef TEASER /* videos (build.sh with TEASER=1): the game's HUD only */
#elif defined(BENCH) || defined(DEBUG_HUD) || !defined(CATCHUP) /* tests and bench: the debug lines (normal builds: the game's HUD, game.c hud_draw) */
        ms(a, sizeof a, "LOGIC", t_logic);
        ms(b, sizeof b, "DRAW", t_draw);
        ms(c, sizeof c, "GS", t_gs);
        snprintf(line, sizeof line, "%s  %s  %s", a, b, c);
        ms(a, sizeof a, "WORST", worst);
        snprintf(line + strlen(line), sizeof line - strlen(line), "  %s MS  TRIS %u  FX %u", a, render_tris_drawn(), fx_count());
        render_text(16, 12, line);
        snprintf(line, sizeof line, "LIFE %d  COINS %d  PURPLE %d/%d  PIECES %d  KURIBO %d  RUNTIME %u KB", game_life(), game_coins(),
                 game_purple(), game_purple_total(), game_starpieces(), game_kuribo_alive(), mem_total() >> 10);
        render_text(16, 26, line);
        snprintf(line, sizeof line, "STARS %d  LIVES %d  CAM %d %s", game_stars(), game_lives(), camera_current(),
                 camera_type_name());
        render_text(16, 40, line);
        if (game_time_left() >= 0) { /* the comet's time left */
            int t = (game_time_left() + 59) / 60;
            snprintf(line, sizeof line, "TIME %d:%02d", t / 60, t % 60);
            render_text(16, 54, line);
        }
#endif
        menu_pointer_draw(); /* the star pointer (StarPointerDirector::draw: last) */
        if ((frame & 255) == 0) {
            char dbg[128];
            game_debug(dbg, sizeof dbg);
            printf("%s\n", dbg);
        }
        if ((frame++ & 255) == 0) {
            printf("frame %u: logic %u us, draw %u us, worst %u us, tris %u\n", frame, (u32)(t_logic / EE_MHZ),
                   (u32)(t_draw / EE_MHZ), (u32)(worst / EE_MHZ), render_tris_drawn());
#ifdef CATCHUP
            { /* the frame rate over the last 256 frames, from the vblanks (console measurements over udptty) */
                static u32 vb_prev;
                u32 dv = vblanks - vb_prev;
                vb_prev = vblanks;
                if (frame > 1 && dv)
                    printf("fps %u.%u over 256 frames (%u vblanks)\n", 256 * 5994 / dv / 100, 256 * 5994 / dv / 10 % 10, dv);
            }
#endif
        }
        u32 vwait = render_wait_cycles(); /* blocked on the VIF in game_draw */
        (void)vwait;
#ifdef SAMPLE_PROF
#ifndef PROF_FROM /* the frames sampled (XF="-DSAMPLE_PROF -DPROF_FROM=2900 -DPROF_TO=3700": a route's stretch) */
#define PROF_FROM 60
#define PROF_TO 1861
#endif
        { extern void prof_start(void), prof_dump(void); if (frame == PROF_FROM) prof_start(); if (frame == PROF_TO) prof_dump(); }
#endif
        t_gs = render_end();
#ifdef BENCH
        {
            extern u32 game_prof[8];
            static u32 psum[9];
            extern u32 prof_pose, prof_skin, prof_draw, game_kcl_draw, game_kcl_drawn, prof_kq, prof_kqn, prof_fxa, prof_shadow;
            extern u32 prof_rest[8];
            static u32 rsum[8];
            if (frame > 60 && frame <= 1860) for (int i = 0; i < 8; i++) rsum[i] += prof_rest[i];
            for (int i = 0; i < 8; i++) prof_rest[i] = 0;
            if (frame == 1861) printf("bench rest: leaves %u vines %u ota %u dino %u sambo %u wplant %u osphere %u foot %u us" "\n",
                                      (u32)(rsum[0] / 1800 / EE_MHZ), (u32)(rsum[1] / 1800 / EE_MHZ), (u32)(rsum[2] / 1800 / EE_MHZ),
                                      (u32)(rsum[3] / 1800 / EE_MHZ), (u32)(rsum[4] / 1800 / EE_MHZ), (u32)(rsum[5] / 1800 / EE_MHZ),
                                      (u32)(rsum[6] / 1800 / EE_MHZ), (u32)(rsum[7] / 1800 / EE_MHZ));
            {
                extern u32 lprof[]; extern const char *const lprof_name[]; extern const int lprof_n;
                static u32 lsum[96];
                for (int i = 0; i < lprof_n && i < 96; i++) { if (frame > 60 && frame <= 1860) lsum[i] += lprof[i]; lprof[i] = 0; }
                if (frame == 1861)
                    for (int i = 0; i < lprof_n && i < 96; i++)
                        if (lsum[i] / 1800 / EE_MHZ >= 100) printf("bench logic %-24s %u us" "\n", lprof_name[i], (u32)(lsum[i] / 1800 / EE_MHZ));
            }
            {
                extern u32 kprof[KPROF_KINDS], kprof_n[KPROF_KINDS];
                static u32 ks[KPROF_KINDS], kn[KPROF_KINDS];
                for (int i = 0; i < KPROF_KINDS; i++) { if (frame > 60 && frame <= 1860) { ks[i] += kprof[i]; kn[i] += kprof_n[i]; } kprof[i] = kprof_n[i] = 0; }
                if (frame == 1861)
                    for (int i = 0; i < KPROF_KINDS; i++)
                        if (kn[i]) printf("bench walker kind %d: %u us, %u a frame" "\n", i, (u32)(ks[i] / 1800 / EE_MHZ), kn[i] / 1800);
            }
            {
                extern u32 prof_dcalls, prof_dculled;
                static u32 dc, du;
                if (frame > 60 && frame <= 1860) { dc += prof_dcalls; du += prof_dculled; }
                prof_dcalls = prof_dculled = 0;
                if (frame == 1861) printf("bench draw_model calls %u a frame, %u culled" "\n", dc / 1800, du / 1800);
            }
            static u32 fsum[2];
            if (frame > 60 && frame <= 1860) { fsum[0] += prof_fxa; fsum[1] += prof_shadow; }
            prof_fxa = prof_shadow = 0;
            if (frame == 1861) printf("bench fxanim %u shadow %u us" "\n", (u32)(fsum[0] / 1800 / EE_MHZ), (u32)(fsum[1] / 1800 / EE_MHZ));
            static u32 ksum[3];
            if (frame > 60 && frame <= 1860) { ksum[0] += prof_kq; ksum[1] += prof_kqn; ksum[2] += game_kcl_drawn; }
            prof_kq = prof_kqn = game_kcl_drawn = 0;
            if (frame == 1861) printf("bench kcl_query: %u us a frame, %u calls (%u in draw)\n", (u32)(ksum[0] / 1800 / EE_MHZ), ksum[1] / 1800, ksum[2] / 1800);
            static u32 qsum[4];
            if (frame > 60 && frame <= 1860) { qsum[0] += prof_pose; qsum[1] += prof_skin; qsum[2] += prof_draw; qsum[3] += game_kcl_draw; }
            prof_pose = prof_skin = prof_draw = game_kcl_draw = 0;
            if (frame == 1861) printf("bench parts: pose %u skin %u draw_model %u kcl-in-draw %u us\n", (u32)(qsum[0] / 1800 / EE_MHZ),
                                      (u32)(qsum[1] / 1800 / EE_MHZ), (u32)(qsum[2] / 1800 / EE_MHZ), (u32)(qsum[3] / 1800 / EE_MHZ));
            { /* ad hoc probes (render.c prof_x): whatever is being measured */
                extern u32 prof_x[8];
                static u64 xs[8];
                for (int i = 0; i < 8; i++) { if (frame > 60 && frame <= 1860) xs[i] += prof_x[i]; prof_x[i] = 0; }
                if (frame == 1861) for (int i = 0; i < 8; i++) if (xs[i]) printf("bench probe %d %u us\n", i, (u32)(xs[i] / 1800 / EE_MHZ));
            }
            static u32 pmax[9];
            if (frame > 60 && frame <= 1860) for (int i = 0; i < 8; i++) if (game_prof[i] > pmax[i]) pmax[i] = game_prof[i];
            if (frame == 1861) for (int i = 0; i < 8; i++) printf("bench draw max %d %u us" "\n", i, (u32)(pmax[i] / EE_MHZ));
            extern u32 render_begin_wait;
            static u64 bwsum;
            if (frame > 60 && frame <= 1860) { for (int i = 0; i < 8; i++) psum[i] += game_prof[i]; psum[8] += vwait; bwsum += render_begin_wait; }
            if (frame == 1861) printf("bench draw beginwait avg %u us (in skies: the flip, VIF and clear waits)\n", (u32)(bwsum / 1800 / EE_MHZ));
            extern u32 anim_inflates, anim_inflated; extern u32 anim_cache_bytes(void);
            if (frame == 60) anim_inflates = anim_inflated = 0;
            if (frame == 1861) printf("bench anim: %u inflates, %u KB in 1800 frames, slot %u B\n", anim_inflates, anim_inflated >> 10, anim_cache_bytes() / 8);
            if (frame == 1861) {
                static const char *pn[9] = {"setup", "skies", "insts", "pickups", "mario", "rest", "fx", "2d", "vifwait"};
                for (int i = 0; i < 9; i++) printf("bench draw %-8s avg %u us\n", pn[i], (u32)(psum[i] / 1800 / EE_MHZ));
            }
        }
        if (frame > 60 && frame <= 1860) {
            u32 v[5] = {t_logic - game_kcl_cycles, game_kcl_cycles, t_draw, t_gs, t_logic + t_draw + t_gs};
            for (int i = 0; i < 5; i++) {
                sum[i] += v[i];
                if (v[i] > top[i]) top[i] = v[i];
            }
        }
        { /* emitted VU1 vertices per path (render.c render_vstat), averaged over the bench */
            extern u32 render_vstat[6];
            static u32 vsum[6];
            for (int i = 0; i < 6; i++) { if (frame > 60 && frame <= 1860) vsum[i] += render_vstat[i]; render_vstat[i] = 0; }
            if (frame == 1861) printf("bench verts: static tex %u lit %u env %u | skinned tex %u lit %u env %u\n",
                                      vsum[0] / 1800, vsum[1] / 1800, vsum[2] / 1800, vsum[3] / 1800, vsum[4] / 1800, vsum[5] / 1800);
        }
        { /* the real frame rate over the measured frames, from the vblanks (render.c flip_tick) */
            extern volatile u32 render_vblanks;
            static u32 vb0;
            if (frame == 61) vb0 = render_vblanks;
            if (frame == 1861 && render_vblanks > vb0)
                printf("bench fps %u.%u (%u vblanks for 1800 frames)\n", 1800 * 5994 / (render_vblanks - vb0) / 100,
                       1800 * 5994 / (render_vblanks - vb0) / 10 % 10, render_vblanks - vb0);
        }
        if (frame == 1861) {
            static const char *nm[5] = {"logic", "kcl", "draw", "gs", "total"};
            for (int i = 0; i < 5; i++)
                printf("bench %-5s avg %u us worst %u us\n", nm[i], (u32)(sum[i] / 1800 / EE_MHZ), (u32)(top[i] / EE_MHZ));
            game_debug(line, sizeof line);
            printf("bench end: %s coins %d purple %d pieces %d kuribo %d\n", line, game_coins(), game_purple(),
                   game_starpieces(), game_kuribo_alive());
            u32 tm, to, ts;
            render_tex_stats(&tm, &to, &ts);
            printf("bench textures: %u uploads, %u without room, %u waits for room\n", tm, to, ts);
        }
#endif
    }
    return 0;
}

#ifndef PACKS
void *__real_memalign(size_t align, size_t n);
void __real_free(void *p);
void *__wrap_memalign(size_t align, size_t n) { return __real_memalign(align, n); }
void __wrap_free(void *p) { __real_free(p); }
#endif
