#ifndef ME_H
#define ME_H

#include <tamtypes.h>

/* ME (music effects): Petari's RhythmLib ME sequencer, its notes fitted to the playing BGM's chord (me.c) */
void me_set_resource(const u8 *table, const u8 *seq); /* AudSystem::setMeResource: JaiMe.arc's metable / defaultme (se_load) */
int me_has(void);                                     /* MR::hasME */
void me_start_system(const char *name);               /* MR::startSystemME */
void me_start_random(const char *stem, int n);        /* <stem>1..n, one at random (SphereSelector::playSelectedME...) */
void me_rhythm(const u8 *rhy, u32 size);               /* the music's .rhy (tools/rhythm.py) took the rhythm; NULL: none */
void me_rhythm_tick(u32 pos, u32 loop);                /* the stream's place (audio_pos) and loop start (bytes): its rhythm events up to it */
void me_update(void);                                  /* per frame: AudRhythmMeSystem::update (AudMeMgr) */
void me_reset(void);                                   /* AudSystem::preProcessToReset: every ME stopped at once */
/* host tests: the chord in use (AudChordInfo) */
int me_chord_notes(u8 out[8]);

#endif
