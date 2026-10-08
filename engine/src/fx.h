#ifndef FX_H
#define FX_H

#include "blob.h"
#include "fx_tab.h"

/* Particle effects (JParticle, tools/jpa.py): a lean port of Petari's JPA emitters and particles. */
void fx_init(const Scene *s);
/* One-shot effect at a world matrix (an instance's: rotation, scale, position). */
void fx_emit(int fx, const float m[16]);
/* Looping effect owned by `key` (e.g. an instance): call every frame it should run; its emitters stop emitting the
 * first frame it is not kept, and go once their particles are gone. */
void fx_keep(int fx, const void *key, const float m[16]);
/* Step every emitter and particle one frame (60 Hz), then draw them (after the models). */
void fx_update(void);
void fx_draw(void);
u32 fx_count(void); /* live particles */

#endif
