/* sndvoice.irx: SPU2 voices for the EE's sound-effect sequencer (engine/src/se.c), over one SIF RPC server.
 *   function 1, batch: u32 n, then n commands {u16 kind, u16 entry, u32 value}: kind 0 sceSdSetParam, 1
 *     sceSdSetSwitch, 2 sceSdSetAddr, 3 sceSdSetCoreAttr; answers {u32 ENDX core 0, u32 ENDX core 1 (voices that
 *     reached a sample end since their key on), u32 voice 0 ENVX, u32 voice 0 NAX}
 *   function 2, upload: u32 SPU address, u32 bytes, then the bytes (a multiple of 64): sceSdVoiceTrans on
 *     transfer channel 0 (audsrv streams on channel 1), waited for
 * libsd is started by audsrv (loaded before this module). */
#include <irx.h>
#include <libsd.h>
#include <loadcore.h>
#include <sifcmd.h>
#include <stdio.h>
#include <thbase.h>

IRX_ID("sndvoice", 1, 0);

#define SNDVOICE_RPC 0x534E4456
#define BUF_SIZE (64 * 1024 + 64)

static unsigned char buf[BUF_SIZE] __attribute__((aligned(64)));
static SifRpcDataQueue_t qd;
static SifRpcServerData_t sd;

static void memcpy_words(unsigned int *d, const unsigned int *s, int n)
{
    while (n--) *d++ = *s++;
}

static void *handler(int fn, void *data, int size)
{
    unsigned int *p = (unsigned int *)data;
    (void)size;
    if (fn == 1) {
        unsigned int n = p[0], i;
        unsigned short *c = (unsigned short *)(p + 1);
        for (i = 0; i < n; i++, c += 4) {
            unsigned int v = c[2] | (unsigned int)c[3] << 16;
            switch (c[0]) {
            case 0: sceSdSetParam(c[1], (u16)v); break;
            case 1: sceSdSetSwitch(c[1], v); break;
            case 2: sceSdSetAddr(c[1], v); break;
            case 3: sceSdSetCoreAttr(c[1], (u16)v); break;
            }
        }
        p[0] = sceSdGetSwitch(0 | SD_SWITCH_ENDX);
        p[1] = sceSdGetSwitch(1 | SD_SWITCH_ENDX);
        p[2] = sceSdGetParam(SD_VOICE(0, 5) | SD_VPARAM_ENVX); /* voice 0's envelope and address (diagnostics) */
        p[3] = sceSdGetAddr(SD_VOICE(0, 5) | SD_VADDR_NAX);
    } else if (fn == 3) { /* read back: u32 SPU address -> 64 bytes (diagnostics) */
        sceSdVoiceTrans(0, SD_TRANS_READ | SD_TRANS_MODE_DMA, (u8 *)(p + 16), (u32 *)p[0], 64);
        sceSdVoiceTransStatus(0, 1);
        memcpy_words(p, p + 16, 16);
    } else if (fn == 2) {
        sceSdVoiceTrans(0, SD_TRANS_WRITE | SD_TRANS_MODE_DMA, (u8 *)(p + 16), (u32 *)p[0], p[1]);
        sceSdVoiceTransStatus(0, 1);
        p[0] = 0;
    }
    return data;
}

static void server(void *arg)
{
    (void)arg;
    sceSifInitRpc(0);
    sceSifSetRpcQueue(&qd, GetThreadId());
    sceSifRegisterRpc(&sd, SNDVOICE_RPC, handler, buf, NULL, NULL, &qd);
    sceSifRpcLoop(&qd);
}

int _start(int argc, char *argv[])
{
    iop_thread_t t; /* fields one by one: an initialiser would call memcpy, which the IOP kernel lacks */
    int id;
    t.attr = TH_C; t.option = 0; t.thread = server; t.stacksize = 0x1000; t.priority = 0x30;
    (void)argc; (void)argv;
    if ((id = CreateThread(&t)) <= 0) return MODULE_NO_RESIDENT_END;
    StartThread(id, NULL);
    printf("sndvoice: ready\n");
    return MODULE_RESIDENT_END;
}
