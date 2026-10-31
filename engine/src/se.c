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

void se_wait(void) /* the last command batch answered (before a scene change lets its buffers go) */
{
    if (pending) { while (SifCheckStatRpc(&cd)) {} endx = recvbuf[0]; pending = 0; }
}
u32 se_endx(void) { return endx; }

#ifdef SND_TEST
#include "me.h"
#include "se_ids.h"
/* the SEs of sound.bin's test set, one every 90 frames, not placed; logs the voices in use */
void se_test(int frame)
{
    static const u32 ids[] = {SE_SY_COIN, SE_SY_STAR_PIECE_GET, SE_PV_JUMP_S, SE_PM_JUMP_M, SE_SY_TALK_OK,
                              SE_SM_TALKBLN_OPEN, SE_EM_KURIBO_STEP1, SE_SY_TICOFAT_POINT};
    if (frame % 90 == 30) {
        u32 id = ids[(frame / 90) % (sizeof ids / sizeof ids[0])];
        u32 h = se_start(id, NULL);
        printf("se test: frame %d start %08x -> handle %u\n", frame, id, h);
    }
    if (frame % 90 == 75) { /* an ME between them (me.c: on the BGM's beat, on its chord) */
        static const char *const me[] = {"ME_MAGIC", "ME_COIN_APPEAR_S", "ME_RALLY_COMBO_FIRST", "ME_MAGIC_L"};
        const char *n = me[(frame / 90) % 4];
        printf("se test: frame %d ME %s (hasME %d)\n", frame, n, me_has());
        me_start_system(n);
    }
    if (frame % 90 >= 30 && frame % 90 <= 60 && frame % 5 == 0) {
        int ns, nc, nv;
        se_stats(&ns, &nc, &nv);
        printf("se test: frame %d sounds %d chans %d voices %d ENDX %06x ENVX %04x\n", frame, ns, nc, nv, se_endx(), recvbuf[2]);
    }
}
#endif
