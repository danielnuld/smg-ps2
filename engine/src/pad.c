#include "pad.h"

/* Same libpad use as D:\ps2\ps2-platformer\src\pad.c, plus analog mode for the left stick. */
static char pad_buf[256] __attribute__((aligned(64)));
static struct padButtonStatus st;
static u32 held, prev, pressed;
static float lx, ly;
static int analog_set;

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
    }
    pressed = held & ~prev;
}

int pad_held(u32 m) { return (held & m) != 0; }
int pad_pressed(u32 m) { return (pressed & m) != 0; }
float pad_lx(void) { return lx; }
float pad_ly(void) { return ly; }
