#ifndef AUDIO_H
#define AUDIO_H

/* Audio (milestone 6): music as a PCM stream through audsrv (tools/music.py .pcm files). */
int audio_init(void);                 /* libsd + audsrv on the IOP; -1 if they would not start */
int audio_music(const char *path);    /* start a music stream (host:music/<name>.pcm), looping */
int audio_ready(void);                /* audsrv started */
void audio_fade(int frames);        /* AudBgm::stop(frames): the music faded out over frames (<= 0: now), then closed */
void audio_stop(void);                /* the music stream closed (a scene change) */
void audio_update(void);              /* per frame: feed what audsrv has room for */

#endif
