#ifdef SAMPLE_PROF
/* A sampling profiler for bench builds (XF=-DSAMPLE_PROF, tools/prof_map.py sums it by function): EE Timer 1
 * interrupts ~10 000 times a second and the interrupted PC (COP0 EPC) is counted in 64-byte buckets of the program;
 * prof_dump prints the busiest buckets. */
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

/* PCSX2's kernel runs the handler after an eret of its own (EPC then 0x81fe0): the interrupted PC is a word of the
 * kernel's RAM (its saved context, 0x80019430 there). The first sample lists the kernel words holding program
 * addresses; over the next CAL samples the one that nearly always holds one and changes the most is taken */
#define CAL 2000
#define CALW 512
extern char _ftext[], _etext[];
static u16 cal_in[CALW], cal_chg[CALW];
static u32 cal_prev[CALW];
static int cal_slot = -1, ncand;
static u32 cand[CALW], cal_addr;
static u16 hcall[NB];
static int tick(int cause)
{
    (void)cause;
    u32 pc;
    __asm__ volatile("mfc0 %0, $14" : "=r"(pc));
    if (on && (pc < BASE || pc >= (u32)_etext)) {
        if (cal_slot < 0 && samples == 0) { /* the kernel's RAM: the words holding program addresses now */
            for (u32 a = 0x80000000u; a < 0x80080000u && ncand < CALW; a += 4) {
                u32 v = *(volatile u32 *)a;
                if (v >= (u32)_ftext && v < (u32)_etext) { cand[ncand] = a; cal_prev[ncand++] = v; }
            }
        } else if (cal_slot < 0 && samples < CAL) {
            for (int i = 0; i < ncand; i++) {
                u32 v = *(volatile u32 *)cand[i];
                if (v >= (u32)_ftext && v < (u32)_etext) cal_in[i]++;
                if (v != cal_prev[i]) cal_chg[i]++;
                cal_prev[i] = v;
            }
        } else if (cal_slot < 0) {
            int best = 0;
            for (int i = 0; i < ncand; i++)
                if (cal_in[i] >= CAL * 9 / 10 && cal_chg[i] > cal_chg[best]) best = i;
            cal_slot = best;
            cal_addr = cand[best];
        }
        if (cal_slot >= 0) pc = *(volatile u32 *)cal_addr;
    }
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

static void dump_hist(const u16 *h, const char *tag, u32 min)
{
    /* six buckets a line, 4 ms apart (udptty loses bursts) */
    char line[200];
    int n = 0;
    u32 k = 0;
    for (u32 i = 0; i < NB; i++) {
        if (h[i] < min) continue;
        n += snprintf(line + n, sizeof line - n, " %06x:%u", (BASE + (i << 6)) & 0xFFFFFF, h[i]);
        if (++k % 6 == 0) {
            printf("%s%s\n", tag, line);
            n = 0;
            u32 c0, c1;
            __asm__ volatile("mfc0 %0, $9" : "=r"(c0));
            do __asm__ volatile("mfc0 %0, $9" : "=r"(c1)); while (c1 - c0 < 294912u * 4);
        }
    }
    if (n) printf("%s%s\n", tag, line);
}

void prof_dump(void)
{
    on = 0;
    printf("prof: %u samples, %u outside the program (%08x %08x %08x %08x), slot %d at %08x of %d (in %u changed %u)\n", samples, outside, seen[0],
           seen[1], seen[2], seen[3], cal_slot, cal_addr, ncand, cal_slot >= 0 ? cal_in[cal_slot] : 0, cal_slot >= 0 ? cal_chg[cal_slot] : 0);
    /* buckets under 1 / 5000 of the samples left out; then the library calls' callers ("callp" lines) */
    u32 min = samples / 5000 > 2 ? samples / 5000 : 2;
    dump_hist(hist, "prof", min);
    dump_hist(hcall, "callp", min);
    printf("prof end\n");
}
#endif
#ifdef SAMPLE_PROF
/* the library calls by caller (Makefile: --wrap with SAMPLE_PROF; snprintf below): one in 16 calls counted at its return address,
 * dumped as "callp" buckets */
static u32 ncalls;
static inline void count_caller(void *ra)
{
    u32 b = ((u32)ra - BASE) >> 6;
    if (on && !(++ncalls & 15) && b < NB && hcall[b] < 0xFFFF) hcall[b]++;
}
int __real_strcmp(const char *, const char *), __real_strncmp(const char *, const char *, unsigned int);
char *__real_strstr(const char *, const char *);
void *__real_memset(void *, int, unsigned int);
float __real_cosf(float), __real_sinf(float);
int __wrap_strcmp(const char *a, const char *b) { count_caller(__builtin_return_address(0)); return __real_strcmp(a, b); }
int __wrap_strncmp(const char *a, const char *b, unsigned int n) { count_caller(__builtin_return_address(0)); return __real_strncmp(a, b, n); }
char *__wrap_strstr(const char *a, const char *b) { count_caller(__builtin_return_address(0)); return __real_strstr(a, b); }
void *__wrap_memset(void *d, int c, unsigned int n) { count_caller(__builtin_return_address(0)); return __real_memset(d, c, n); }
float __wrap_cosf(float x) { count_caller(__builtin_return_address(0)); return __real_cosf(x); }
float __wrap_sinf(float x) { count_caller(__builtin_return_address(0)); return __real_sinf(x); }
#endif
#ifdef SAMPLE_PROF
#include <stdarg.h>
int __wrap_snprintf(char *s, unsigned int n, const char *f, ...)
{
    count_caller(__builtin_return_address(0));
    va_list ap;
    va_start(ap, f);
    int r = vsnprintf(s, n, f, ap);
    va_end(ap);
    return r;
}
#endif
#ifdef BOUNDS
/* XF=-DBOUNDS (Makefile: -fsanitize=bounds-strict): every index past a known array bound, read or write, logged once
 * a source line with its caller, instead of picking up whatever the layout puts next */
#include <stdio.h>
typedef struct { const char *file; unsigned line, column; } UbLoc;
typedef struct { UbLoc loc; const void *array_type, *index_type; } UbOob;
void __ubsan_handle_out_of_bounds(UbOob *d, unsigned long index)
{
    static unsigned seen[64];
    static int nseen;
    unsigned key = d->loc.line ^ (unsigned)(unsigned long)d->loc.file << 16;
    for (int i = 0; i < nseen; i++) if (seen[i] == key) return;
    if (nseen < 64) seen[nseen++] = key;
    printf("BOUNDS: %s:%u:%u index %ld (from %08x)\n", d->loc.file, d->loc.line, d->loc.column, (long)index,
           (unsigned)(unsigned long)__builtin_return_address(0));
}
void __ubsan_handle_out_of_bounds_abort(UbOob *d, unsigned long index) { __ubsan_handle_out_of_bounds(d, index); }
#endif
