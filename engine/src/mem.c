#include "mem.h"

#include <stdio.h>
#include <string.h>

/* ponytail: one static arena, no free; a scene is loaded once. Per-scene reset comes with scene changes. */
#define ARENA_SIZE (6 << 20)
static u8 arena[ARENA_SIZE] __attribute__((aligned(64)));
static u32 top;
static u32 used[MEM_TYPES];
static const char *names[MEM_TYPES] = {"render", "objects", "player", "collision", "scene"};

void *mem_alloc(int type, u32 size)
{
    size = (size + 15) & ~15u;
    if (top + size > ARENA_SIZE) {
        printf("mem: out of memory (%s, %u B, used %u)\n", names[type], size, top);
        for (;;) {}
    }
    void *p = arena + top;
    top += size;
    used[type] += size;
    memset(p, 0, size);
    return p;
}

static u32 external;
void mem_charge(int type, u32 size) { used[type] += size; external += size; }
u32 mem_used(int type) { return used[type]; }
u32 mem_total(void) { return top + external; }
const char *mem_name(int type) { return names[type]; }

void mem_report(void)
{
    printf("mem: runtime %u B (arena %u + library buffers %u)\n", top + external, top, external);
    for (int i = 0; i < MEM_TYPES; i++)
        printf("mem:   %-9s %8u B\n", names[i], used[i]);
}
