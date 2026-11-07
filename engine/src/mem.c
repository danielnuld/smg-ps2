#include "mem.h"

#include <stdio.h>
#include <string.h>

/* One static arena, no free: a bump allocator with marks (mem_mark / mem_release) for the streamed zone packs. */
/* 5.25 MB: the most seen in use was 4.43 MB (PhantomGalaxy/3 at boot; the cover lines' runtime, arena + library
 * buffers, at most 5084 KB in KoopaBattleVs3/1); the 0.75 MB under the old 6 MB went to the packs' region */
#define ARENA_SIZE (21 << 18)
static u8 arena[ARENA_SIZE] __attribute__((aligned(64)));
static u32 top, peak;
static u32 used[MEM_TYPES];
static const char *names[MEM_TYPES] = {"render", "objects", "player", "collision", "scene"};

void *mem_alloc(int type, u32 size)
{
    size = (size + 15) & ~15u;
    if (top + size > ARENA_SIZE) {
#ifdef _EE /* shown on screen too (render.c) */
        void render_fatal(const char *fmt, ...) __attribute__((noreturn));
        render_fatal("mem: out of memory (%s, %u B, used %u)", names[type], size, top);
#else
        printf("mem: out of memory (%s, %u B, used %u)\n", names[type], size, top);
        for (;;) {}
#endif
    }
    void *p = arena + top;
    top += size;
    if (top > peak) peak = top;
    used[type] += size;
    memset(p, 0, size);
    return p;
}

void mem_mark(MemMark *m)
{
    m->top = top;
    memcpy(m->used, used, sizeof used);
}

void mem_release(const MemMark *m)
{
    top = m->top;
    memcpy(used, m->used, sizeof used);
}

static u32 external;
void mem_charge(int type, u32 size) { used[type] += size; external += size; }
u32 mem_used(int type) { return used[type]; }
u32 mem_total(void) { return top + external; }
u32 mem_peak(void) { return peak; }
const char *mem_name(int type) { return names[type]; }

void mem_report(void)
{
    printf("mem: runtime %u B (arena %u, peak %u of %u + library buffers %u)\n", top + external, top, peak, ARENA_SIZE, external);
    for (int i = 0; i < MEM_TYPES; i++)
        printf("mem:   %-9s %8u B\n", names[i], used[i]);
}
