#include "audio.h"

#include <audsrv.h>
#include <kernel.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

/* IOP modules linked into the ELF (assets.S, copied from the SDK by build.sh) */
extern const unsigned char libsd_irx[], libsd_irx_end[], audsrv_irx[], audsrv_irx_end[];

static FILE *music;
static unsigned int music_loop;    /* byte offset of the loop start in the samples */
static int ready;
static unsigned char chunk[8192] __attribute__((aligned(64)));

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
    if (music) fclose(music);
    music = fopen(path, "rb");
    if (!music || fread(hdr, 4, 4, music) != 4 || memcmp(hdr, "SMGM", 4) != 0) {
        printf("audio: no music %s\n", path);
        if (music) fclose(music);
        music = NULL;
        return -1;
    }
    struct audsrv_fmt_t fmt = {(int)hdr[1], 16, (int)hdr[2]};
    music_loop = hdr[3];
    audsrv_set_format(&fmt);
    audsrv_set_volume(MAX_VOLUME);
    printf("audio: %s, %u Hz, loop at byte %u\n", path, hdr[1], music_loop);
    return 0;
}

void audio_update(void)
{
    if (!music) return;
    int room = audsrv_available();
    while (room >= 1024) { /* whole frames of 4 bytes, at most a chunk per call */
        int want = room < (int)sizeof chunk ? room & ~3 : (int)sizeof chunk;
        int got = fread(chunk, 1, want, music);
        if (got > 0) {
            audsrv_play_audio((const char *)chunk, got & ~3);
            room -= got & ~3;
        }
        if (got < want) /* the end: back to the loop start (the 16-byte header first) */
            fseek(music, 16 + music_loop, SEEK_SET);
    }
}
