#include "pad.h"

/* Same libpad use as D:\ps2\ps2-platformer\src\pad.c, plus analog mode for the left stick. */
static char pad_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus st;
static u32 held, prev, pressed;
static float lx, ly, rx, ry;
static int analog_set;
u32 pad_force; /* test builds: buttons held as well as the pad's */

int pad_init(void)
{
    padInit(0);
    if (padPortOpen(0, 0, pad_buf) == 0)
        return -1;
    int s, tries = 0;
    do {
        s = padGetState(0, 0);
    } while (s != PAD_STATE_STABLE && s != PAD_STATE_FINDCTP1 && s != PAD_STATE_DISCONN && ++tries < 500000);
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
    held |= pad_force;
    pressed = held & ~prev;
#ifdef BENCH
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
