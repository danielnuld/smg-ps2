/* me.c (Petari's ME sequencer) on hand-made data: a chord table with C major and G major, a rhythm of beats, one ME
 * whose track picks the chord's root on the first beat and holds it two beats. The note lands on the chord (raised
 * into its start range), follows a chord change, and the ME ends; no rhythm, no ME. */
// SRCS: -Ifake
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/me.h"

/* se_seq.c's notes, recorded */
static int notes, offs, last_key, last_bank, last_prog;
u32 se_note(int bank, int prog, int key, int vel, const u8 *adsr, u16 release)
{
    (void)vel; (void)adsr; (void)release;
    last_key = key; last_bank = bank; last_prog = prog;
    return ++notes;
}
void se_note_off(u32 h, u16 dr) { (void)h; (void)dr; offs++; }
int se_note_alive(u32 h) { (void)h; return 1; }
void se_note_params(u32 h, float vol, float pitch, float pan, float oct) { (void)h; (void)vol; (void)pitch; (void)pan; (void)oct; }

static void be32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void be16(u8 *p, u32 v) { p[0] = v >> 8; p[1] = v; }

static u8 rhy[512], table[64], seq[64];
static u32 rhy_n;

static void make(void)
{
    /* .rhy: header, CIT {0, 'CITS', 0, u16 2 chords, u16 1 scale, offsets, chords, scale {up, down}, its notes} */
    u8 *cit = rhy + 24;
    memcpy(cit + 4, "CITS", 4);
    be16(cit + 12, 2); be16(cit + 14, 1);
    be32(cit + 16, 32); be32(cit + 20, 40); be32(cit + 24, 48);
    const u8 c[8] = {0x7F, 0, 4, 7, 0x7F, 0x7F, 0x7F, 0x7F}, g[8] = {0x7F, 7, 11, 2, 0x7F, 0x7F, 0x7F, 0x7F};
    memcpy(cit + 32, c, 8); memcpy(cit + 40, g, 8);
    be32(cit + 48, 56); be32(cit + 52, 68);
    const u8 major[12] = {0, 2, 4, 5, 7, 9, 11, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F};
    memcpy(cit + 56, major, 12); memcpy(cit + 68, major, 12);
    u32 cit_n = 80, ev = 24 + cit_n, n = 0;
    u32 *e = (u32 *)(rhy + ev);
    for (u32 b = 0; b < 8; b++) { /* a type 0 beat every 8000 samples; G major just after beat 4 */
        e[n++] = (b * 8000) >> 1 << 10 | 0 << 8 | 1;
        if (b == 4) e[n++] = (b * 8000 + 100) >> 1 << 10 | 1 << 8 | 1;
    }
    memcpy(rhy, "SMGR", 4);
    u32 h[5] = {7, 24, cit_n, ev, n};
    memcpy(rhy + 4, h, sizeof h);
    rhy_n = ev + 4 * n;

    /* metable.bmt: 1 entry {prio, volume, _2, _3, u16 fade}, its name */
    be32(table, 1); be32(table + 4, 12); be32(table + 8, 20);
    const u8 par[8] = {100, 100, 64, 0, 0, 30, 0, 0};
    memcpy(table + 12, par, 8);
    be32(table + 20, 24);
    strcpy((char *)table + 24, "ME_TEST");

    /* defaultme.bme: {1, start 8}; main: start on a type 0 beat, open track 0, end. Track 0: bank 3, program 5,
     * a note on the chord's root at velocity 100, wait two type 0 beats, end. The note: chord data {1 entry, in
     * order, root} */
    be32(seq, 1); be32(seq + 4, 8);
    const u8 code[] = {0x20, 0, 0x10, 0, 0, 0, 16, 0x24,                          /* 8: main */
                       0x00, 3, 0x01, 5, 0x40, 0, 0, 30, 100, 0x28, 0, 0, 2, 0x24, /* 16: track 0 */
                       0x70, 0x81, 0, 0, 36, 0x71,                               /* 30: the note's definition */
                       1, 0, 0x02};                                              /* 36: chord data */
    memcpy(seq + 8, code, sizeof code);
}

int main(void)
{
    make();
    me_set_resource(table, seq);

    me_start_system("ME_TEST");
    me_update();
    assert(notes == 0); /* no rhythm: no chord, no ME */

    me_rhythm(rhy, rhy_n);
    assert(me_has());
    u8 cn[8];
    me_chord_notes(cn);
    assert(cn[0] == 0 && cn[1] == 4 && cn[2] == 7 && cn[3] == 0x7F);
    me_rhythm_tick(0, 0xFFFFFFFFu); /* the first tick: its place (beat 0 passed without callbacks) */
    me_start_system("ME_TEST");
    me_update();                              /* AudMe::update: the track started */
    me_rhythm_tick(8000 * 4 * 1, 0xFFFFFFFFu); /* beat 1: cmdStart waits for a type 0 beat */
    assert(notes == 0);
    me_rhythm_tick(8000 * 4 * 2, 0xFFFFFFFFu); /* beat 2: track 0 opens and plays the root */
    assert(notes == 1 && last_key == 60 && last_bank == 3 && last_prog == 5);
    me_rhythm_tick(8000 * 4 * 3, 0xFFFFFFFFu); /* beat 3 */
    assert(offs == 0);
    me_rhythm_tick(8000 * 4 * 4, 0xFFFFFFFFu); /* beat 4: two beats waited, the track ends, its note released */
    assert(offs >= 1);
    for (int i = 0; i < 4; i++) me_update();   /* the ME freed */

    me_rhythm_tick(8000 * 4 * 4 + 400 * 4, 0xFFFFFFFFu); /* past the chord change: G major */
    me_chord_notes(cn);
    assert(cn[0] == 2 && cn[1] == 7 && cn[2] == 11);
    me_start_system("ME_TEST");
    me_update();
    me_rhythm_tick(8000 * 4 * 6, 0xFFFFFFFFu); /* beats 5 and 6 */
    assert(notes == 2 && last_key == 67);      /* G's root, in the start range 60..72 */

    me_rhythm(NULL, 0);
    assert(!me_has());
    printf("me ok\n");
    return 0;
}
