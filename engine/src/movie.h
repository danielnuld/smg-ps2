#ifndef MOVIE_H
#define MOVIE_H

#include <tamtypes.h>

/* a movie's file read in order: open (its bytes), start reading the next n bytes into dst (64-aligned, n whole
 * sectors), done: that read finished */
typedef struct {
    int (*open)(const char *path, u32 *size);
    void (*start)(u8 *dst, u32 n);
    int (*done)(void);
    void (*close)(void);
} MovieIo;

/* MoviePlayingSequence: packs/movies/<name>.smv (tools/movies.py) played on the IPU at 29.97 fps with its sound, the
 * whole screen, nothing else running; its memory from malloc (no scene loaded). 0: not there */
int movie_play(const char *name, const MovieIo *io);
/* ponytail: no MovieSubtitles (CinemaSuper with Layout_StoryDemo*Talk at sSubtitlesInfo's frames): the font and the
 * layouts are let go with the scene while a movie plays (its memory is the packs' region) */

#endif
