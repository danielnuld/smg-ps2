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

#include "audio.h"
#include "blob.h"
#include "camera.h"
#include "game.h"
#include "mem.h"
#include "pad.h"
#include "render.h"

#include "fx.h"
#include "music_tab.h"
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

#ifdef DISC
#define SECTOR 2048
#define PACK_ALLOC(n) (((n) + SECTOR - 1) & ~(SECTOR - 1))
typedef struct { char path[88]; u32 off, size; } TocEntry;
static TocEntry toc[72];
static int ntoc;
static u32 bin_lsn;
static sceCdRMode cd_mode = {5, SCECdSpinNom, SCECdSecS2048, 0};

/* whole sectors from `lsn` into `dst` (64-aligned, PACK_ALLOC long); wait: block until they are in */
static int cd_read(u32 lsn, u32 sectors, void *dst, int wait)
{
    if (!sceCdRead(lsn, sectors, dst, &cd_mode))
        return 0;
    if (wait) {
        sceCdSync(0);
        InvalidDCache(dst, (u8 *)dst + sectors * SECTOR - 1);
    }
    return 1;
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
    sceCdInit(SCECdINoD);
    sceCdMmode(SCECdMmodeDvd);
    if (!sceCdSearchFile(&ft, "\\PACKS.TOC;1") || !sceCdSearchFile(&fb, "\\PACKS.BIN;1")) {
        printf("packs: no PACKS.TOC / PACKS.BIN on the disc\n");
        for (;;) {}
    }
    bin_lsn = fb.lsn;
    u8 *t = memalign(64, PACK_ALLOC(ft.size));
    if (!t || !cd_read(ft.lsn, PACK_ALLOC(ft.size) / SECTOR, t, 1)) {
        printf("packs: cannot read PACKS.TOC\n");
        for (;;) {}
    }
    u32 n = *(const u32 *)t;
    disc_menu(t + 4, n);
    size_t lp = strlen(scen);
    for (u32 i = 0; i < n; i++) {
        const TocEntry *e = (const TocEntry *)(t + 4 + i * sizeof(TocEntry));
        if ((strncmp(e->path, scen, lp) == 0 && e->path[lp] == '/') || strcmp(e->path, "resident.bin") == 0) {
            if (ntoc == (int)(sizeof toc / sizeof toc[0])) { printf("packs: TOC full\n"); for (;;) {} }
            memcpy(&toc[ntoc++], e, sizeof *e);
        }
    }
    free(t);
    printf("packs: disc TOC %u entries, %d for %s\n", n, ntoc, scen);
}
#else
#define PACK_ALLOC(n) (n)
#endif

static int pack_open(Pack *p, const char *path)
{
    memset(p, 0, sizeof *p);
#ifdef DISC
    for (int i = 0; i < ntoc; i++)
        if (strcmp(toc[i].path, path) == 0) {
            p->base = bin_lsn + toc[i].off / SECTOR;
            p->size = toc[i].size;
            p->f = (FILE *)1;
            return 1;
        }
    return 0;
#else
    char full[160];
    snprintf(full, sizeof full, "host:packs/%s", path);
    if (!(p->f = fopen(full, "rb")))
        return 0;
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
    if (fread(dst, 1, n, p->f) != n)
        return 0;
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

static const u8 *load_file(const char *path)
{
    Pack p;
    if (!pack_open(&p, path)) {
        printf("packs: missing %s\n", path);
        for (;;) {}
    }
    u8 *b = memalign(64, PACK_ALLOC(p.size));
    if (!b || !pack_read(&p, b, p.size)) {
        printf("packs: cannot read %s (%u B)\n", path, p.size);
        for (;;) {}
    }
    pack_close(&p);
    pack_bytes += p.size;
    return b;
}

static const u8 *zone_files[64];      /* the stage packs in use, one per zone */
static unsigned char zone_full[64];    /* 1: that stage's .bin is in use, 0: its .low.bin */
static const u8 *scene_blob;
static u32 nzones, base_models, base_tex, base_kcl;
static MemMark base_mark;

/* Next zone's packs, read a slice per frame (a launch star's flight hides it); stages whose pack does not change
 * keep their buffer. ponytail: the old and the new packs are both in RAM until the swap */
#define READ_STEP (32 << 10) /* ~4.5 ms of host: read per frame; 3 MB fits a 300-frame flight */
static struct {
    int zone, active;                  /* target zone */
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
    if (pend.cur) free(pend.cur);
    for (u32 z = 0; z < pend.z && z < nzones; z++)
        if (pend.buf[z] && pend.buf[z] != zone_files[z]) free((void *)pend.buf[z]);
    memset(&pend, 0, sizeof pend);
}

static void pend_start(int zone)
{
    pend_cancel();
    pend.zone = zone;
    pend.active = 1;
    chain_of(zone, pend.full);
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
                printf("packs: missing %s" "\n", path);
                for (;;) {}
            }
            pend.size = pend.f.size;
            pend.cur = memalign(64, PACK_ALLOC(pend.size));
            pend.off = 0;
        }
#ifdef DISC
        if (pend.flight) { /* the asynchronous read: still going, or in */
            if (!all && sceCdSync(1))
                return 0;
            sceCdSync(0);
            InvalidDCache(pend.cur + pend.off, pend.cur + pend.off + PACK_ALLOC(pend.flight) - 1);
            pend.off += pend.flight;
            pend.f.pos += pend.flight;
            pend.bytes += pend.flight;
            pend.flight = 0;
            if (!all) budget = 0; /* one read per frame */
        } else if (budget && !all && pend.off < pend.size) { /* start the next one, poll it later */
            u32 n = pend.size - pend.off < budget ? pend.size - pend.off : budget;
            if (!cd_read(pend.f.base + pend.off / SECTOR, PACK_ALLOC(n) / SECTOR, pend.cur + pend.off, 0)) {
                printf("packs: read error" "\n");
                for (;;) {}
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
            printf("packs: read error" "\n");
            for (;;) {}
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
        if (zone_files[z] && zone_files[z] != pend.buf[z]) free((void *)zone_files[z]);
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
            printf("packs: zone %d failed" "\n", pend.zone);
            for (;;) {}
        }
    render_load_textures(s, base_tex, base_models);
    game_rebind();
    printf("packs: zone %d, %u models, %u textures, %u KB read before, swap %u us" "\n", pend.zone, s->nmodels, s->ntex,
           pend.bytes >> 10, (u32)((cycles() - t0) / 294.912f));
    memset(&pend, 0, sizeof pend);
}

/* Per frame: prepare the zone the game expects next, swap when it is wanted (reading the rest at once if needed). */
static void stream_update(Scene *s)
{
    int next = game_zone_next(zone_full), want = game_zone_wanted(zone_full);
    if (want >= 0 && (!pend.active || pend.zone != want))
        pend_start(want);
    else if (want < 0 && next >= 0 && (!pend.active || pend.zone != next))
        pend_start(next);
    if (!pend.active)
        return;
    if (pend_step(want >= 0 ? 0xFFFFFFFF : READ_STEP) && want >= 0)
        pend_commit(s);
}

static const u8 *load_packs(Scene *s)
{
    char path[160];
    pick_scenario();
#ifdef DISC
    toc_load();
#endif
    snprintf(path, sizeof path, "%s/scene.bin", scen);
    scene_blob = load_file(path);
    if (blob_load(s, scene_blob) != 0)
        return NULL;
    if (blob_add(s, load_file("resident.bin")) != 0)
        return NULL;
    snprintf(path, sizeof path, "%s/common.bin", scen);
    if (blob_add(s, load_file(path)) != 0)
        return NULL;
    u32 sz;
    nzones = *(const u32 *)blob_chunk(scene_blob, "ZONE", &sz);
    if (nzones > 64)
        return NULL;
    base_models = s->nmodels;
    base_tex = s->ntex;
    base_kcl = s->nkcl;
    printf("packs: %s, %u zones, base %u models, %u textures, %u KB read\n", scen, nzones, s->nmodels, s->ntex,
           pack_bytes >> 10);
    return scene_blob;
}
#endif

#define EE_MHZ 294.912f

static void load_modules(void)
{
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
}

static void ms(char *out, int n, const char *label, u32 c)
{
    u32 us = (u32)(c / EE_MHZ);
    snprintf(out, n, "%s %u.%02u", label, us / 1000, (us % 1000) / 10);
}

int main(void)
{
    load_modules();
    audio_init();
    render_init();
    pad_init();
#ifdef PACKS
    const u8 *blob = load_packs(&scene);
#else
    const u8 *blob = blob_load(&scene, slice_blob) == 0 ? slice_blob : NULL;
#endif
    if (!blob) {
        printf("slice: bad blob\n");
        for (;;) {}
    }
#ifdef PACKS
    /* base packs first, then the game (its allocations stay), then the zone packs above the mark */
    render_load_textures(&scene, 0, 0);
    game_init(&scene, blob);
    mem_mark(&base_mark);
    {
        /* the scenario's stage BGM (music_tab.h); sequenced ones (LibraryRoom...) are not streams: galaxy 01 */
        const char *track = "SMG_galaxy01_strm";
        char path[96];
        for (u32 i = 0; i < sizeof music_tab / sizeof music_tab[0]; i++)
            if (!strcmp(music_tab[i].scen, scen)) track = music_tab[i].track;
        snprintf(path, sizeof path, "host:music/%s.pcm", track);
        audio_music(path);
    }
    pend_start(scene_start(&scene)->zone);
    pend_step(0xFFFFFFFF);
    pend_commit(&scene);
#else
    render_load_textures(&scene, 0, 0);
    game_init(&scene, blob);
#endif
    mem_report();

    u32 t_logic = 0, t_draw = 0, t_gs = 0, worst = 0, frame = 0;
#ifdef BENCH
    /* frames 60..1859 of the scripted path: sums and worsts of logic (minus KCL), KCL, draw, GS finish, total */
    u64 sum[5] = {0};
    u32 top[5] = {0};
#endif
    char line[128], a[24], b[24], c[24];
    for (;;) {
        u32 t0 = cycles();
        game_kcl_cycles = 0;
        pad_update();
        game_update();
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
#ifdef PLANT_TEST
        if (frame >= 120 && frame % 10 == 0) {
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
        if (frame > 120 && frame <= 220 && frame % 5 == 0) {
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
        /* coverage run (tools/coverage.ps1): a launch star at frame 60 if there is one, then one line at 900 */
        if (frame == 60)
            game_test_spindriver(0);
        {
            static u32 prev, slow;
            if (frame > 120 && prev && t0 - prev > (u32)(20000 * EE_MHZ)) slow++;
            prev = t0;
            if (frame == 900)
                printf("cover: %s ok, slow %u, worst %u us, runtime %u KB, lives %d\n", scen, slow,
                       (u32)(worst / EE_MHZ), mem_total() >> 10, game_lives());
        }
#endif
        stream_update(&scene);
        audio_update();
#endif
        u32 t1 = cycles();
        game_draw();
        u32 t2 = cycles();
        t_logic = t1 - t0;
        t_draw = t2 - t1;
        if (t2 - t0 > worst && frame > 60) worst = t2 - t0;
#ifdef TEASER /* videos (build.sh with TEASER=1): a plain counter instead of the debug lines */
        snprintf(line, sizeof line, "COINS %d   STAR BITS %d   LIVES %d", game_coins(), game_starpieces(), game_lives());
        render_text(16, 12, line);
#else
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
#endif
        if ((frame & 255) == 0) {
            char dbg[128];
            game_debug(dbg, sizeof dbg);
            printf("%s\n", dbg);
        }
        if ((frame++ & 255) == 0)
            printf("frame %u: logic %u us, draw %u us, worst %u us, tris %u\n", frame, (u32)(t_logic / EE_MHZ),
                   (u32)(t_draw / EE_MHZ), (u32)(worst / EE_MHZ), render_tris_drawn());
        t_gs = render_end();
#ifdef BENCH
        if (frame > 60 && frame <= 1860) {
            u32 v[5] = {t_logic - game_kcl_cycles, game_kcl_cycles, t_draw, t_gs, t_logic + t_draw + t_gs};
            for (int i = 0; i < 5; i++) {
                sum[i] += v[i];
                if (v[i] > top[i]) top[i] = v[i];
            }
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
