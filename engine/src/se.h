#ifndef SE_H
#define SE_H

#include <tamtypes.h>

/* Sound effects on SPU2 voices (sndvoice.irx, engine/iop/sndvoice): core 0's 24 voices play SPU ADPCM waves
 * uploaded to SPU RAM; the EE queues voice settings during a frame and sends them in one RPC (se_flush). */
int se_init(void);                                    /* after audio_init: load sndvoice.irx, bind its RPC server */
int se_upload(u32 spu_addr, const void *data, u32 n); /* ADPCM to SPU RAM (blocking; n rounded up to 64) */
void se_param(u16 entry, u16 value);                  /* sceSdSetParam(entry, value), queued */
void se_switch(u16 entry, u32 value);                 /* sceSdSetSwitch */
void se_addr(u16 entry, u32 value);                   /* sceSdSetAddr */
void se_flush(void);                                  /* per frame: send the queue (waits for the last one) */
u32 se_endx(void);                                    /* core 0 voices that reached a sample end (last flush) */
#ifdef SND_TEST
void se_test(int frame);                              /* a tone on voice 0 every 2 s; logs ENDX */
#endif

#endif
