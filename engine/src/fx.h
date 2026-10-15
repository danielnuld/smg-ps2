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
/* A skinned instance's clip ("<class>/<clip>") went from frame `from` (-1: it just started) to `to`: the effects its
 * AutoEffectList ties to that clip (EffectKeeper: AnimName, StartFrame / EndFrame, JointName, Offset, ScaleValue) at
 * m * world[joint]; key: the instance (kept effects) */
void fx_anim(const char *clip, float from, float to, const float m[16], const float (*world)[16], const void *key);
/* emitEffect(actor, name): the AutoEffectList row of class `cls` (any case) with that UniqueName, at m * world[its joint]
 * (world NULL: m) and its offset / scale; key NULL a one-shot, else kept while called each frame (deleteEffect: stop
 * calling). 0 if the pack has no such row (tools/jpa.py anim_effects: skinned classes and NAMED) */
int fx_named(const char *cls, const char *name, const float m[16], const float (*world)[16], const void *key);
/* Step every emitter and particle one frame (60 Hz), then draw them (after the models). */
void fx_update(void);
void fx_draw(void);
u32 fx_count(void); /* live particles */

/* the scene texture of WaterPlant type k (0-3, packed with the effects' textures), -1 none */
int fx_waterplant_tex(int k);
/* Plant's stalk texture (PlantStalk.bti, after the WaterPlant ones), -1 none */
int fx_plantstalk_tex(void);
/* DinoPackun's footprint texture (DinoPackunFootprint.bti, after PlantStalk's), -1 none */
int fx_dinofoot_tex(void);
int fx_ocean_tex(int k); /* OceanSphere's face (0) and back (1) textures (tools/jpa.py) */

#endif
