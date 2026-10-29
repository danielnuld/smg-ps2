#include "pad.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Same libpad use as D:\ps2\ps2-platformer\src\pad.c, plus analog mode for the left stick. */
static char pad_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus st;
static u32 held, prev, pressed;
static float lx, ly, rx, ry;
static int analog_set;
#ifdef REPLAY
static unsigned char *rep;
static int rep_n, rep_i, rep_on;
void pad_replay_begin(void) /* the game hands control to the player: records from here (u16 buttons, s8 lx ly rx ry) */
{
    if (rep_on) return;
    FILE *f = fopen("host:replay.bin", "rb");
    if (!f) { printf("replay: no host:replay.bin\n"); return; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    rep = malloc(n);
    rep_n = rep && fread(rep, 1, n, f) == (size_t)n ? (int)(n / 6) : 0;
    fclose(f);
    rep_on = 1;
    printf("replay: %d records\n", rep_n);
}
#endif
u32 pad_force; /* test builds: buttons held as well as the pad's */
int pad_force_stick; float pad_force_lx, pad_force_ly; /* test builds: the left stick instead of the pad's */

/* --- WPadRumble / WPadRumbleData (Petari System/WPadRumble.cpp): eight channels each playing a pattern (one bit a
 * WPad::update, here a logic step); the motor on while any channel's bit is; a pattern already playing is not restarted
 * (findRubmlePattern matches the pattern alone); after 1800 steps of rumbling it rests until the count runs down.
 * The Wii Remote's one motor is the DualShock's small one (on / off: the patterns are its strength); RUMBLE_BIG the
 * large one's level beside it (ponytail: 0, a calibration knob: the Wii motor is between the two). */
#define RUMBLE_BIG 0
static const struct { const char *name, *bits; } rumble_tab[RUMBLE_N] = { /* gRumblePatternTable, in RumblePad order */
    {"最強【長】", "1001001010101111111111111111111111111111111111111111111111111111111111111111111111101010101001001001"},
    {"最強", "111111111111111111111000000000"},
    {"強", "011011011011011011010000000000"},
    {"中", "011010011001011011010000000000"},
    {"弱", "100101001010010100100000000000"},
    {"微弱", "100001000010000001000100000000"},
    {"マリオ[スリップアップ]", "1010011001100110010010101"},
    {"マリオ[亀投げ]", "0001101011"},
    {"マリオ[ホッパーため]", "11011"},
    {"マリオ[変身]", "101001100110011001001010011001100111010010100110011001111110"},
    {"マリオ[変身解除]", "101111100010111100001011100000"},
    {"マリオ[しびれ]", "101011010110101101010101001010010100101010101101011010110101"},
    {"マリオ[つぶれ]", "101011010110101101010101001010010100101010101101011010110101101011010110101101010101001010010100101010101101011010110101"},
};
static struct { const char *bits; int n, step, on, order; } rch[8]; /* RumbleChannel: _0 (bits, n frames), _C, _E, _8 */
static int r_order = 1, r_motor, r_count, r_rest, r_rest_on, r_sent = -1, act_ready; /* _C, _8, _B0, _B4, _B8 */

int pad_rumble_pattern(const char *name)
{
    for (int i = 0; i < RUMBLE_N; i++)
        if (!strcmp(rumble_tab[i].name, name)) return i;
    return -1;
}

int pad_rumble(int pat) /* vibratePatternIfNotExist -> setRumblePatternIfNotExist(.., false) */
{
    if (pat < 0 || pat >= RUMBLE_N) return 0;
    int empty = -1, oldest = -1;
    for (int i = 0; i < 8; i++) { /* findRubmlePattern */
        if (!rch[i].bits) { if (empty < 0) empty = i; continue; }
        if (rch[i].bits == rumble_tab[pat].bits) return 0;
        if (oldest < 0 || rch[i].order < rch[oldest].order) oldest = i;
    }
    int c = empty >= 0 ? empty : oldest;
    r_order = (r_order + 1) & 0x7FFFFFFF;
    rch[c].bits = rumble_tab[pat].bits; rch[c].n = (int)strlen(rch[c].bits); rch[c].step = 0; rch[c].order = r_order;
    return 1;
}

void pad_rumble_stop(void) /* WPadRumble::stop (GamePauseSequence, resetAllControllerRumble at a scene change) */
{
    r_motor = 0;
    memset(rch, 0, sizeof rch);
}

void pad_rumble_update(void) /* WPadRumble::updateRumble */
{
    int b = 0;
    for (int i = 0; i < 8; i++) { /* RumbleChannel::update (no looping patterns: _4 is always false here) */
        if (!rch[i].bits) continue;
        if (rch[i].step >= rch[i].n) { memset(&rch[i], 0, sizeof rch[i]); continue; }
        rch[i].on = rch[i].bits[rch[i].step++] == '1';
        b |= rch[i].on;
    }
    if (r_rest_on) {
        if (--r_count <= 0) { r_rest_on = 0; r_count = 0; }
        r_motor = 0; /* pause */
    } else {
        r_motor = b;
        if (b) r_rest = 5;
        else {
            r_rest = r_rest - 1 < -9 ? -9 : r_rest - 1 > 5 ? 5 : r_rest - 1;
            if (r_rest < 0) { r_count = 0; return; }
        }
        if (++r_count > 1800) { r_rest_on = 1; r_count = 1800; }
    }
}

/* the motor state to the pad once it is in DualShock mode with actuators (padSetActAlign once, then ActDirect on a
 * change) */
static void rumble_send(int s)
{
    if (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1) return;
    if (!act_ready) {
        if (padGetReqState(0, 0) != PAD_RSTAT_COMPLETE || padInfoAct(0, 0, -1, 0) <= 0) return;
        static const char align[6] = {0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
        padSetActAlign(0, 0, align);
        act_ready = 1;
        return;
    }
    int m = r_motor;
    if (m == r_sent) return;
    char act[6] = {(char)m, (char)(m ? RUMBLE_BIG : 0), 0, 0, 0, 0};
    if (padSetActDirect(0, 0, act)) r_sent = m;
}

int pad_init(void)
{
    padInit(0);
    if (padPortOpen(0, 0, pad_buf) == 0)
        return -1;
    int s, tries = 0;
    do {
        s = padGetState(0, 0);
    } while (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1 && s != PAD_STATE_DISCONN && ++tries < 500000);
    printf("pad: init state %d after %d tries\n", s, tries);
    return 0;
}

static float axis(unsigned char v)
{
    float f = ((int)v - 128) / 127.0f;
    return (f > -0.2f && f < 0.2f) ? 0.0f : (f > 1 ? 1 : f < -1 ? -1 : f);
}

void pad_update(void)
{
    int s = padGetState(0, 0);
    if ((s == PAD_STATE_STABLE || s == PAD_STATE_FINDCTP1) && !analog_set) {
        padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
        analog_set = 1;
    }
    prev = held;
    if ((s == PAD_STATE_STABLE || s == PAD_STATE_FINDCTP1) && padRead(0, 0, &st) != 0) {
        held = 0xFFFF ^ st.btns; /* libpad: 0 = pressed */
        lx = axis(st.ljoy_h);
        ly = -axis(st.ljoy_v);
        rx = axis(st.rjoy_h);
        ry = -axis(st.rjoy_v);
    }
    pad_rumble_update(); /* WPad::update: _18->update() */
    rumble_send(s);
    held |= pad_force;
    if (pad_force_stick) { lx = pad_force_lx; ly = pad_force_ly; }
    pressed = held & ~prev;
#ifdef REPLAY
    if (rep_on && rep && rep_i < rep_n) { /* host:replay.bin (tools/replay.py): one record a logic step */
        const unsigned char *e = rep + 6 * rep_i++;
        held = e[0] | e[1] << 8;
        lx = (signed char)e[2] / 127.0f; ly = (signed char)e[3] / 127.0f;
        rx = (signed char)e[4] / 127.0f; ry = (signed char)e[5] / 127.0f;
        pressed = held & ~prev;
        if (rep_i == rep_n) printf("replay: end at record %d\n", rep_i);
    }
#endif
#if defined(BENCH) && !defined(BENCH_STILL) /* BENCH_STILL: the same counts with Mario left at the start */
    /* bench_mode: "still" written over "path_" in a copy of the ELF gives BENCH_STILL without a second build */
    static volatile const char bench_mode[16] = "BENCHMODE:path_";
    if (bench_mode[10] == 's') return;
    /* fixed path, 30 s (milestone 3): forward with chained jumps (X every 4 frames for 85 frames out of 180: the
     * 2nd and 3rd jumps), a right-hand circle with spins and a long jump every 2 s (R1 + X), forward-left with a
     * jump every 1.5 s and a back flip every 3 s (R1 + X standing still) */
    static u32 f;
    u32 t = f++;
    prev = held;
    held = 0;
    lx = t < 600 ? 0.0f : t < 1200 ? 1.0f : -0.7f;
    ly = t < 600 ? 1.0f : t < 1200 ? 0.5f : 0.7f;
    if (t < 600 && t % 180 >= 45 && t % 180 < 130 && (t & 2)) held |= PAD_CROSS;
    if (t >= 600 && t < 1200 && t % 120 == 0) held |= PAD_SQUARE;
    if (t >= 600 && t < 1200 && t % 120 >= 50 && t % 120 <= 62) held |= PAD_R1;
    if (t >= 600 && t < 1200 && t % 120 == 60) held |= PAD_CROSS;
    if (t >= 1200 && t % 90 == 45) held |= PAD_CROSS;
    if (t >= 1200 && t % 180 >= 140) { lx = ly = 0.0f; held |= PAD_R1; } /* 0.95^30: ratio < 0.3 */
    if (t >= 1200 && t % 180 == 170) held |= PAD_CROSS;
    pressed = held & ~prev;
#endif
}

int pad_held(u32 m) { return (held & m) != 0; }
int pad_pressed(u32 m) { return (pressed & m) != 0; }
void pad_eat(u32 m) { pressed &= ~m; }
float pad_lx(void) { return lx; }
float pad_ly(void) { return ly; }
float pad_rx(void) { return rx; }
float pad_ry(void) { return ry; }
