#include "mc.h"

#include <kernel.h>
#include <libmc.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

/* The save on the memory card (SaveDataHandleSequence's NAND save): mc0:/SMGPS2/save.dat holds the six
 * files' image (gamedata.c gd_image) and its MR::calcCheckSum after it; icon.sys and save.icn (tools/ps2icon.py,
 * linked by assets.S) give it its picture in the browser. Loaded once at boot, written in steps (the card checked,
 * mkdir, each file opened, written, closed) polled each frame so the game goes on while it saves. Outside the game
 * modules' sections: it, the boot's result and the no-save choice live over scene changes. The card states stand for
 * NANDResultCode's (menu.c sv_*): no card / unformatted and too little room for the save are CheckEnableToCreate's
 * (NAND_02), a bad image or checksum isSaveDataCorrupted (NAND_07), a failed write NAND_11_2.
 * The card in port 0, else the one in port 1 (chosen at boot: mc1:/SMGPS2 then).
 * ponytail: the card's state checked at boot and before each save, not watched between */
#define MC_DIR "/SMGPS2"
#define MC_FILE "/SMGPS2/save.dat"
#define MC_MAX (48 * 1024) /* the six files with Mario's and Luigi's data (gamedata.c files) */

extern const u8 save_icn[], save_icn_end[], icon_sys[], icon_sys_end[];

static u8 buf[MC_MAX + 4] __attribute__((aligned(64)));
static int ok, port, step, fd, len, busy, pending, file, have, nosave, result, error, boot;
static const u8 *pend_src;

static u32 check_sum(const u8 *p, int size) /* MR::calcCheckSum */
{
    u16 sum = 0, inv = 0;
    for (int i = 0; i + 1 < size; i += 2) { u16 v = (u16)(p[i] | p[i + 1] << 8); sum += v; inv += (u16)~v; }
    return (u32)sum << 16 | inv;
}

int mc_init(void)
{
    if (SifLoadModule("rom0:MCMAN", 0, NULL) < 0 || SifLoadModule("rom0:MCSERV", 0, NULL) < 0) { printf("mc: modules not loaded\n"); return -1; }
    if (mcInit(MC_TYPE_MC) < 0) { printf("mc: init failed\n"); return -1; }
    ok = 1;
    return 0;
}

static int sync_now(void) { int cmd, res; mcSync(0, &cmd, &res); return res; }

/* the card's state from mcGetInfo: MC_NOCARD (none, not a PS2 card, unformatted), MC_FULL (no save there and too
 * little room for one: the folder, icon.sys, the icon, the save, in 1 KB clusters), else MC_OK */
static int card_state(int res, int type, int room, int format)
{
#ifdef MC_FAKE /* a build with XF=-DMC_FAKE=MC_NOCARD / MC_FULL / MC_CORRUPT / MC_FAILED fakes that card state */
    if (MC_FAKE == MC_NOCARD || (MC_FAKE == MC_FULL && !have)) return MC_FAKE;
#endif
    if (res < -2 || type != MC_TYPE_PS2 || !format) return MC_NOCARD;
    int need = 3 + (int)(icon_sys_end - icon_sys + 1023) / 1024 + (int)(save_icn_end - save_icn + 1023) / 1024 +
               (MC_MAX + 4 + 1023) / 1024;
    if (!have && room < need) return MC_FULL;
    return MC_OK;
}

int mc_check(void)
{
    if (!ok) return MC_NOCARD;
    int type = 0, room = 0, format = 0;
    mcGetInfo(port, 0, &type, &room, &format);
    int st = card_state(sync_now(), type, room, format);
    return st == MC_OK ? MC_NOFILE : st;
}

int mc_load(u8 *dst, int size)
{
    if (!ok) return MC_NOCARD;
    int type = 0, room = 0, format = 0, st;
    for (port = 0;; port++) { /* port 1's card when port 0 has none */
        mcGetInfo(port, 0, &type, &room, &format);
        st = card_state(sync_now(), type, room, format);
        if (st != MC_NOCARD || port == 1) break;
    }
    if (st == MC_NOCARD) { port = 0; printf("mc: no card\n"); return MC_NOCARD; }
    mcOpen(port, 0, MC_FILE, 1); /* O_RDONLY */
    int f = sync_now();
    if (f < 0) { printf("mc: no save\n"); return st == MC_FULL ? MC_FULL : MC_NOFILE; }
    have = 1; /* the save's room is taken: it is written over */
    int n = size < MC_MAX ? size : MC_MAX;
    mcRead(f, buf, n + 4);
    int got = sync_now();
    mcClose(f);
    sync_now();
    u32 sum;
    memcpy(&sum, buf + n, 4);
#ifdef MC_FAKE
    if (MC_FAKE == MC_CORRUPT) got = 0;
#endif
    if (got != n && (got != n + 4 || sum != check_sum(buf, n))) { /* (n bytes: a save written before the checksum) */
        printf("mc: save corrupted (%d bytes, %d expected)\n", got, n + 4);
        return MC_CORRUPT;
    }
    memcpy(dst, buf, n);
    printf("mc: save loaded (%d bytes)\n", n);
    return MC_OK;
}

void mc_remove(void) /* NANDErrorSequence::exeRemove: requestRemoveSaveData */
{
    if (!ok) return;
    mcDelete(port, 0, MC_FILE);
    printf("mc: corrupted save removed (%d)\n", sync_now());
    have = 0;
}

static void save_end(int res);
void mc_save(const u8 *src, int size)
{
    if (nosave) { result = MC_OK; return; } /* SaveDataHandleSequence::tryNoSave */
    if (!ok || size > MC_MAX) { save_end(ok ? MC_FAILED : MC_NOCARD); return; }
    if (busy) { pending = 1; pend_src = src; return; } /* a newer image: saved again once this one is written */
    memcpy(buf, src, size);
    u32 sum = check_sum(buf, size);
    memcpy(buf + size, &sum, 4);
    len = size + 4;
    busy = 1; step = 0; file = have ? 2 : 0; /* icon.sys and the icon with a new save only */
}

static void save_end(int res)
{
    busy = 0;
    result = res;
    if (res != MC_OK) { error = res; pending = 0; printf("mc: save failed (%d)\n", res); return; }
    have = 1;
    printf("mc: saved (%d bytes)\n", len);
    if (pending) { pending = 0; mc_save(pend_src, len - 4); }
}

void mc_poll(void)
{
    static int type, room, format;
    static const char *const names[3] = {"/SMGPS2/icon.sys", "/SMGPS2/save.icn", MC_FILE};
    if (!busy) return;
    const u8 *data = file == 0 ? icon_sys : file == 1 ? save_icn : buf;
    int size = file == 0 ? (int)(icon_sys_end - icon_sys) : file == 1 ? (int)(save_icn_end - save_icn) : len;
    int cmd, res;
    if (step % 2 == 1) { /* an operation running: wait for it */
        if (mcSync(1, &cmd, &res) == 0) return; /* still running */
        if (step == 1) { int st = card_state(res, type, room, format); if (st != MC_OK) { save_end(st); return; } }
        if (step == 5 && res < 0) { save_end(MC_FAILED); return; }
        if (step == 5) fd = res;
#ifdef MC_FAKE
        if (step == 7 && MC_FAKE == MC_FAILED) res = -1;
#endif
        if (step == 7 && res != size) { mcClose(fd); sync_now(); save_end(MC_FAILED); return; }
        step++;
    }
    switch (step) {
    case 0: mcGetInfo(port, 0, &type, &room, &format); step = 1; break;
    case 2: mcMkDir(port, 0, MC_DIR); step = 3; break; /* (it may exist already) */
    case 4: mcOpen(port, 0, names[file], 0x0200 | 0x0002); step = 5; break; /* O_CREAT | O_WRONLY */
    case 6: mcWrite(fd, data, size); step = 7; break;
    case 8: mcClose(fd); step = 9; break;
    case 10:
        if (file < 2) { file++; step = 4; break; }
        save_end(MC_OK);
        break;
    }
}

int mc_busy(void) { return busy; }
int mc_result(void) { return result; }
int mc_take_error(void) { int e = error; error = 0; return e; }
void mc_set_boot(int res) { boot = res; }
int mc_take_boot(void) { int b = boot; boot = MC_OK; return b; }
void mc_set_nosave(void) { nosave = 1; }
int mc_nosave(void) { return nosave; }

void mc_exit_to_browser(void) /* GameSystemFunction::requestGoWiiMenu: the PS2 browser (its memory card screen) */
{
    static char arg[] = "BootBrowser";
    char *argv[1] = {arg};
    printf("mc: to the browser\n");
    SifExitRpc();
    LoadExecPS2("rom0:OSDSYS", 1, argv);
}
