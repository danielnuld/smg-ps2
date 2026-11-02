#ifndef AUDIO_H
#define AUDIO_H

/* Audio (milestone 6): music as a PCM stream through audsrv (tools/music.py .pcm files). */
int audio_init(void);                 /* libsd + audsrv on the IOP; -1 if they would not start */
int audio_music(const char *path);    /* start a music stream (host:music/<name>.pcm), looping */
int audio_ready(void);
int audio_playing(void);                /* audsrv started */
void audio_fade(int frames);        /* AudBgm::stop(frames): the music faded out over frames (<= 0: now), then closed */
void audio_move_volume(float v, int frames); /* MR::moveVolumeStageBGM: the stream's volume to v (0..1) over frames */
unsigned audio_pos(void);            /* the stream's place: its bytes after the header played (about) */
void audio_seek(unsigned pos, int ahead, int fade_in); /* AudBgmVolumeController's recover: on at pos plus ahead frames' worth (past its end: wrapped to the loop), faded in over fade_in frames */
void audio_stop(void);                /* the music stream closed (a scene change) */
void audio_update(void);              /* per frame: feed what audsrv has room for */
void audio_feed(void);   /* the stream into audsrv only (its 10 KB last 80 ms: called between a frame's long parts) */

#endif
