#ifndef MEM_H
#define MEM_H

#include <tamtypes.h>

/* Runtime memory: one bump arena, every byte charged to a type (the phase-1 measurement). */
enum MemType { MEM_RENDER, MEM_OBJECTS, MEM_PLAYER, MEM_COLLISION, MEM_SCENE, MEM_TYPES };

void *mem_alloc(int type, u32 size);
/* Charge memory allocated elsewhere (library buffers) to a type. */
void mem_charge(int type, u32 size);
u32 mem_used(int type);
u32 mem_total(void);
const char *mem_name(int type);
void mem_report(void);

#endif
