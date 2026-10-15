#include "se.h"

#include <kernel.h>
#include <libsd-common.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

extern const unsigned char sndvoice_irx[], sndvoice_irx_end[];

#define SNDVOICE_RPC 0x534E4456
#define MAX_CMDS 1024
#define CHUNK (64 * 1024)

static SifRpcClientData_t cd;
static int ready;
static struct { u32 n; u16 c[MAX_CMDS * 4]; } q __attribute__((aligned(64)));
static u32 sendbuf[(MAX_CMDS * 8 + 64) / 4] __attribute__((aligned(64)));
static u32 recvbuf[16] __attribute__((aligned(64)));
static u32 upbuf[(CHUNK + 64) / 4] __attribute__((aligned(64)));
static u32 endx;
static int pending;

int se_init(void)
{
    int ret;
    if (SifExecModuleBuffer((void *)sndvoice_irx, sndvoice_irx_end - sndvoice_irx, 0, NULL, &ret) < 0) {
        printf("se: sndvoice.irx not loaded\n");
        return -1;
    }
    for (int i = 0; i < 1000; i++) {
        if (SifBindRpc(&cd, SNDVOICE_RPC, 0) < 0) return -1;
        if (cd.server) break;
        nopdelay();
    }
    if (!cd.server) { printf("se: no server\n"); return -1; }
    ready = 1;
    /* core 0's voices dry to its output, its volume full; core 1 mixes core 0 (external), audsrv's PCM (memory
     * input) and its own voices, dry */
    se_switch(0 | SD_SWITCH_VMIXL, 0xFFFFFF);
    se_switch(0 | SD_SWITCH_VMIXR, 0xFFFFFF);
    se_switch(0 | SD_SWITCH_VMIXEL, 0);
    se_switch(0 | SD_SWITCH_VMIXER, 0);
    se_param(0 | SD_PARAM_MMIX, 0xCC0);
    se_param(1 | SD_PARAM_MMIX, 0xCCC);
    se_param(0 | SD_PARAM_MVOLL, 0x3FFF);
    se_param(0 | SD_PARAM_MVOLR, 0x3FFF);
    se_flush();
    printf("se: sndvoice bound\n");
    return 0;
}

int se_upload(u32 spu_addr, const void *data, u32 n)
{
    if (!ready) return -1;
    se_flush();
    if (pending) { while (SifCheckStatRpc(&cd)) {} pending = 0; }
    for (u32 o = 0; o < n; o += CHUNK) {
        u32 k = n - o < CHUNK ? n - o : CHUNK, k64 = (k + 63) & ~63u;
        upbuf[0] = spu_addr + o;
        upbuf[1] = k64;
        memcpy(upbuf + 16, (const u8 *)data + o, k);
        memset((u8 *)(upbuf + 16) + k, 0, k64 - k);
        SifCallRpc(&cd, 2, 0, upbuf, 64 + k64, recvbuf, 16, NULL, NULL);
    }
    return 0;
}

static void push(u16 kind, u16 entry, u32 v)
{
    if (!ready || q.n >= MAX_CMDS) return;
    u16 *c = q.c + 4 * q.n++;
    c[0] = kind; c[1] = entry; c[2] = (u16)v; c[3] = (u16)(v >> 16);
}
void se_param(u16 entry, u16 value) { push(0, entry, value); }
void se_switch(u16 entry, u32 value) { push(1, entry, value); }
void se_addr(u16 entry, u32 value) { push(2, entry, value); }

void se_flush(void)
{
    if (!ready) return;
    if (pending) {
        while (SifCheckStatRpc(&cd)) {}
        endx = recvbuf[0];
        pending = 0;
    }
    sendbuf[0] = q.n; /* sent even empty: its answer is this frame's ENDX */
    memcpy(sendbuf + 1, q.c, q.n * 8);
    SifCallRpc(&cd, 1, SIF_RPC_M_NOWAIT, sendbuf, 4 + q.n * 8, recvbuf, 16, NULL, NULL);
    pending = 1;
    q.n = 0;
}

u32 se_endx(void) { return endx; }

#ifdef SND_TEST
/* a 1 s 880 Hz tone (sound.py spu_adpcm, 22050 Hz, one-shot) at SPU 0x20000 on voice 0, every 2 s */
#include <stdlib.h>
void se_test(int frame)
{
    static int loaded;
    if (!loaded) {
        FILE *f = fopen("host:sndtest.adp", "rb");
        if (!f) { printf("se test: no host:sndtest.adp\n"); loaded = -1; return; }
        static u8 data[64 * 1024] __attribute__((aligned(64)));
        int n = fread(data, 1, sizeof data, f);
        fclose(f);
        se_upload(0x20000, data, n);
        loaded = 1;
        printf("se test: %d bytes uploaded\n", n);
        upbuf[0] = 0x20000;
        SifCallRpc(&cd, 3, 0, upbuf, 64, recvbuf, 64, NULL, NULL);
        printf("se test: SPU 0x20000 reads %08x %08x %08x %08x, sent %08x %08x %08x %08x\n", recvbuf[0], recvbuf[1],
               recvbuf[2], recvbuf[3], ((u32 *)data)[0], ((u32 *)data)[1], ((u32 *)data)[2], ((u32 *)data)[3]);
    }
    if (loaded < 0) return;
    if (frame % 120 == 10) {
        se_param(SD_VOICE(0, 5) | SD_VPARAM_VOLL, 0x3FFF);
        se_param(SD_VOICE(0, 5) | SD_VPARAM_VOLR, 0x3FFF);
        se_param(SD_VOICE(0, 5) | SD_VPARAM_PITCH, 22050 * 4096 / 48000);
        se_param(SD_VOICE(0, 5) | SD_VPARAM_ADSR1, 0x000F);
        se_param(SD_VOICE(0, 5) | SD_VPARAM_ADSR2, 0x1FC0);
        se_addr(SD_VOICE(0, 5) | SD_VADDR_SSA, 0x20000);
        se_switch(0 | SD_SWITCH_KON, 1 << 5);
        printf("se test: key on at frame %d\n", frame);
    }
    if (frame % 120 == 30 || frame % 120 == 100) printf("se test: frame %d ENDX %06x ENVX %04x NAX %06x\n", frame, se_endx(), recvbuf[2], recvbuf[3]);
}
#endif
