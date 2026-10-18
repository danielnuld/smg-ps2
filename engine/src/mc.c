#include "mc.h"

#include <kernel.h>
#include <libmc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

/* The save on the memory card in port 0 (SaveDataHandleSequence's NAND save): mc0:/SMGPS2/save.dat holds the six
 * files' image (gamedata.c gd_image). Loaded once at boot, written in steps (mkdir, open, write, close) polled each
 * frame so the game goes on while it saves. Outside the game modules' sections: it lives over scene changes.
 * ponytail: no icon.sys / icon (the browser shows the folder without a picture), port 0 only, no "card full" window */
#define MC_DIR "/SMGPS2"
#define MC_FILE "/SMGPS2/save.dat"
#define MC_MAX (48 * 1024) /* the six files with Mario's and Luigi's data (gamedata.c files) */

static u8 buf[MC_MAX] __attribute__((aligned(64)));
static int ok, step, fd, len, busy, pending;
static const u8 *pend_src;

int mc_init(void)
{
    if (SifLoadModule("rom0:MCMAN", 0, NULL) < 0 || SifLoadModule("rom0:MCSERV", 0, NULL) < 0) { printf("mc: modules not loaded\n"); return -1; }
    if (mcInit(MC_TYPE_MC) < 0) { printf("mc: init failed\n"); return -1; }
    ok = 1;
    return 0;
}

static int sync_now(void) { int cmd, res; mcSync(0, &cmd, &res); return res; }

int mc_load(u8 *dst, int size)
{
    if (!ok) return 0;
    int type, free, format;
    mcGetInfo(0, 0, &type, &free, &format);
    sync_now();
    mcOpen(0, 0, MC_FILE, 1); /* O_RDONLY */
    int f = sync_now();
    if (f < 0) { printf("mc: no save\n"); return 0; }
    int n = size < MC_MAX ? size : MC_MAX;
    mcRead(f, buf, n);
    int got = sync_now();
    mcClose(f);
    sync_now();
    if (got != n) { printf("mc: save of %d bytes, %d expected\n", got, n); return 0; }
    memcpy(dst, buf, n);
    printf("mc: save loaded (%d bytes)\n", n);
    return n;
}

void mc_save(const u8 *src, int size)
{
    if (!ok || size > MC_MAX) return;
    if (busy) { pending = 1; pend_src = src; return; } /* a newer image: saved again once this one is written */
    memcpy(buf, src, size);
    len = size;
    busy = 1; step = 0;
}

int mc_busy(void) { return busy; }

void mc_poll(void)
{
    if (!busy) return;
    int cmd, res;
    if (step > 0 && step % 2 == 0) { /* an operation running: wait for it */
        if (mcSync(1, &cmd, &res) == 0) return; /* still running */
        if (step == 4 && res < 0) { printf("mc: open failed %d\n", res); busy = 0; return; }
        if (step == 4) fd = res;
        if (step == 6 && res != len) printf("mc: wrote %d of %d\n", res, len);
        step++;
    }
    switch (step) {
    case 0: mcMkDir(0, 0, MC_DIR); step = 2; break;              /* (it may exist already) */
    case 3: mcOpen(0, 0, MC_FILE, 0x0200 | 0x0002); step = 4; break; /* O_CREAT | O_WRONLY */
    case 5: mcWrite(fd, buf, len); step = 6; break;
    case 7: mcClose(fd); step = 8; break;
    case 9:
        busy = 0;
        printf("mc: saved (%d bytes)\n", len);
        if (pending) { pending = 0; mc_save(pend_src, len); }
        break;
    }
}
