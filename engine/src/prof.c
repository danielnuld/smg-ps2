/* A sampling profiler for bench builds: EE Timer 1 interrupts ~10 000 times a second and the interrupted PC (COP0
 * EPC) is counted in 64-byte buckets of the program; prof_dump prints the busiest buckets (tools: map them to
 * functions with the ELF's nm). Only with -DSAMPLE_PROF. */
#ifdef SAMPLE_PROF
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <tamtypes.h>

#define T1_COUNT ((volatile u32 *)0x10000800)
#define T1_MODE ((volatile u32 *)0x10000810)
#define T1_COMP ((volatile u32 *)0x10000820)
#define BASE 0x00100000u
#define NB (0x00C00000u >> 6) /* 12 MB of program in 64-byte buckets */

static u16 hist[NB];
static u32 samples, outside, seen[8];
static int on;

static int tick(int cause)
{
    (void)cause;
    u32 pc;
    __asm__ volatile("mfc0 %0, $14" : "=r"(pc));
    if (on) {
        u32 b = (pc - BASE) >> 6;
        if (pc >= BASE && b < NB) { if (hist[b] < 0xFFFF) hist[b]++; }
        else { if (outside < 8) seen[outside] = pc; outside++; }
        samples++;
    }
    *T1_MODE |= 1 << 10; /* EQUF: clear the compare flag */
    ExitHandler();
    return 0;
}

void prof_start(void)
{
    memset(hist, 0, sizeof hist);
    samples = outside = 0;
    AddIntcHandler(INTC_TIM1, tick, 0);
    *T1_COUNT = 0;
    *T1_COMP = 921;                                  /* bus clock / 16 = 9.216 MHz -> ~10 kHz */
    *T1_MODE = 1 | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 10); /* /16, ZRET, CUE, CMPE, clear EQUF */
    EnableIntc(INTC_TIM1);
    on = 1;
}

void prof_dump(void)
{
    on = 0;
    printf("prof: %u samples, %u outside the program (%08x %08x %08x %08x)\n", samples, outside, seen[0], seen[1], seen[2], seen[3]);
    for (u32 i = 0; i < NB; i++)
        if (hist[i] >= 2) printf("prof %08x %u\n", BASE + (i << 6), hist[i]);
}
#endif
