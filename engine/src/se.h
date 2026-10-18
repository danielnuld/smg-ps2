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
/* the sequencer (se_seq.c, JAudio2 after Petari) on sound.bin (tools/sound.py pack); ids from se_ids.h */
int se_load(const u8 *sound_bin);                     /* tables in EE, the waves uploaded to SPU RAM */
u32 se_compact(const u8 *sound_bin, u32 size);        /* after se_load: drops the uploaded ADPCM -> bytes kept */
u32 se_start(u32 id, const float *pos);               /* a one-shot SE (pos NULL: not placed) -> handle, 0 none */
u32 se_level(u32 id, const float *pos, u32 handle);   /* a level SE: kept while asked each frame -> its handle */
void se_stop(u32 handle);
void se_map_code(int code); /* the player's ground sound code (AudSoundObject::getMapCode), -1 none */
void se_port(u32 handle, int port, u16 value);        /* a sequence port (JAISound::setPort) */
void se_listener(const float eye[3], const float right[3]);
void se_volpitch(u32 handle, float vol, float pitch); /* scale a playing sound's volume and pitch */
/* animation sounds (.bas): clip "<archive>/<clip>" lower case went from frame `from` (-1 just started) to `to` */
void se_wait(void);    /* the last se_flush answered */
void se_silence(void); /* every voice off (before a scene change) */
int se_anim(const char *clip, float from, float to, const float pos[3], const void *owner);
void se_update(int steps);
void se_stats(int *sounds, int *chans, int *voices);                                 /* per frame: 400.36 / 60 sub-frames, then the voices */
#ifdef SND_TEST
void se_test(int frame);                              /* a tone on voice 0 every 2 s; logs ENDX */
#endif

#endif
