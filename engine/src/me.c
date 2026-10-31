/* ME (music effects, MR::startSystemME): Petari's RhythmLib ME sequencer. AudChordInfo (the playing BGM's chord and
 * scale from JaiChord.arc), AudMeSeqReader / AudMeSeqCtrl / AudMeSeqParser (JaiMe.arc's defaultme.bme), AudMeTrack
 * (notes picked on the chord / scale, started through se_seq.c's se_note as JASBank::noteOn would), AudMe / AudMeMgr
 * (the playing parameters of metable.bmt, timed fades) and the system AudMeObject (8 handles).
 * The rhythm: on the Wii the BGM's rhythm track calls the ME tracks back at its note-ons and sets the chord / scale
 * (AudRhythmSeqParser); here tools/rhythm.py keeps those events against the stream's samples (<stream>.rhy) and
 * me_rhythm_tick dispatches them as the stream passes them.
 * ponytail: one rhythm (the stream playing: the Wii's dominant BGM); no pause flag (AudRhythmMeSystem::setIdle);
 * no channel priorities (se_seq has no stealing); the MEs' fx mix / dolby are kept but not heard. */
#include "me.h"

#include "se.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }

/* ---- AudChordInfo ---- */
#define OCTAVE 12
#define NULL_NOTE 0x7F
#define NUM_CHORD_NOTES 8
enum { ScaleType_None, ScaleType_Up, ScaleType_Down, ScaleType_UpDown };
static struct {
    const u8 *res;                 /* the .cit (AudChordTable): mLoaded = res != NULL */
    int chord_count, scale_count;
    const u8 *cur_chord;           /* AudChordData {bass, chord tone[4], add note[3]} */
    const u8 *up, *down;           /* mCurScale (AudScaleData) */
    u8 note_list[NUM_CHORD_NOTES]; /* mChordNoteList */
    s32 cur_chord_index, cur_scale_index;
    s16 table_id;
} ci = {.table_id = -1};

static int ci_available(void) { return ci.res && ci.cur_chord && ci.up; }
static u8 getRoot(void) { return ci.cur_chord[1]; }
static u8 getChordTone(int i) { return ci.cur_chord[1 + i]; }
static u8 getAddNote(int i) { return ci.cur_chord[5 + i]; }
static s32 getScaleNoteUp(s32 i) { return ci.up[i]; }
static s32 getScaleNoteDown(s32 i) { return ci.down[i]; }
static u8 getBassNote(void) { return ci.cur_chord[0] == NULL_NOTE ? ci.cur_chord[1] : ci.cur_chord[0]; }

static void ci_init_params(void) /* initParams */
{
    ci.cur_chord = NULL;
    ci.up = ci.down = NULL;
    ci.table_id = -1;
    for (int i = 0; i < NUM_CHORD_NOTES; i++) ci.note_list[i] = NULL_NOTE;
}

static void expandChordNote(void)
{
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) ci.note_list[i] = NULL_NOTE;
    s32 idx = 0;
    for (s32 i = 0; i < 4; i++) ci.note_list[idx++] = getChordTone(i);
    for (s32 i = 0; i < 3; i++) ci.note_list[idx++] = getAddNote(i);
    ci.note_list[idx] = getBassNote();
    s32 lastSwap = NUM_CHORD_NOTES - 1;
    while (lastSwap >= 1) {
        s32 newLastSwap = -1;
        for (s32 i = 1; i <= lastSwap; i++)
            if (ci.note_list[i] < ci.note_list[i - 1]) {
                u8 tmp = ci.note_list[i];
                ci.note_list[i] = ci.note_list[i - 1];
                ci.note_list[i - 1] = tmp;
                newLastSwap = i - 1;
            }
        lastSwap = newLastSwap;
    }
    for (s32 i = 0; i < NUM_CHORD_NOTES - 1; i++)
        if (ci.note_list[i] == ci.note_list[i + 1]) {
            for (s32 j = i + 1; j < NUM_CHORD_NOTES - 1; j++) ci.note_list[j] = ci.note_list[j + 1];
            ci.note_list[NUM_CHORD_NOTES - 1] = NULL_NOTE;
        }
}

static int setCurChord(s32 index)
{
    if (!ci.res || index >= ci.chord_count) return 0;
    ci.cur_chord = ci.res + be32(ci.res + 16 + 4 * index);
    expandChordNote();
    ci.cur_chord_index = index;
    return 1;
}

static int setCurScale(s32 index)
{
    if (!ci.res || index >= ci.scale_count) return 0;
    const u8 *s = ci.res + be32(ci.res + 16 + 4 * ci.chord_count + 4 * index); /* {u8 *up, u8 *down}, relocated */
    ci.up = ci.res + be32(s);
    ci.down = ci.res + be32(s + 4);
    ci.cur_scale_index = index;
    return 1;
}

/* loadChordInfo(id, false) on AudChordTable::setChordTableResource: {initialized, 'CITS', ?, u16 chords, u16 scales,
 * chord offsets, scale offsets} */
static int loadChordInfo(s16 id, const u8 *res)
{
    ci_init_params();
    ci.res = NULL;
    if (id < 0 || !res || memcmp(res + 4, "CITS", 4)) return 0;
    ci.res = res;
    ci.chord_count = be16(res + 12);
    ci.scale_count = be16(res + 14);
    setCurChord(0);
    setCurScale(0);
    ci.table_id = id;
    return 1;
}

static u8 ci_interval(u8 lo_add, u8 hi_add) /* getThird / getFifth / getSeventh: a chord tone lo..hi over the root */
{
    if (!ci_available()) return NULL_NOTE;
    u8 root = getRoot(), lo = root + lo_add, hi = root + hi_add;
    for (s32 i = 0; i < 4; i++) {
        u8 note = getChordTone(i);
        if (note == NULL_NOTE) continue;
        if (note < root) note += OCTAVE;
        if (note >= lo && note <= hi) return note % OCTAVE;
    }
    return NULL_NOTE;
}
static u8 getThird(void) { return ci_interval(3, 5); }
static u8 getFifth(void) { return ci_interval(6, 8); }
static u8 getSeventh(void) { return ci_interval(9, 11); }
static u8 getSixth(void)
{
    if (!ci_available()) return NULL_NOTE;
    u8 root = getRoot(), lo = root + 8, hi = root + 9;
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        u8 note = ci.note_list[i];
        if (note == NULL_NOTE) continue;
        if (note < root) note += OCTAVE;
        if (note >= lo && note <= hi) return note % OCTAVE;
    }
    return NULL_NOTE;
}
static u8 getNinth(void)
{
    if (!ci_available()) return NULL_NOTE;
    u8 root = getRoot(), lo = root + 1, hi = root + 3;
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        u8 note = ci.note_list[i];
        if (note == NULL_NOTE) continue;
        if (note < root) note += OCTAVE;
        if (note >= lo && note <= hi && note != getThird()) return note % OCTAVE;
    }
    return NULL_NOTE;
}
static u8 getEleventh(void)
{
    if (!ci_available()) return NULL_NOTE;
    u8 root = getRoot(), lo = root + 5, hi = root + 6;
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        u8 note = ci.note_list[i];
        if (note == NULL_NOTE) continue;
        if (note < root) note += OCTAVE;
        if (note >= lo && note <= hi) return note % OCTAVE;
    }
    return NULL_NOTE;
}

static int isOnChord(u8 note, int includeAdd, int includeBass)
{
    s32 pitchClass = note % OCTAVE;
    for (s32 i = 0; i < 4; i++)
        if (pitchClass == getChordTone(i)) return 1;
    if (includeAdd)
        for (s32 i = 0; i < 3; i++)
            if (pitchClass == getAddNote(i)) return 1;
    return includeBass && pitchClass == getBassNote();
}

static u8 getNearestChordNoteDir(u8 note, int up)
{
    if (isOnChord(note, 1, 1)) return note;
    s32 octave = note / OCTAVE, pitchClass = note % OCTAVE, minDist = 0x7FFF, nearest = -1;
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++) {
        s32 n = ci.note_list[i], orig = n;
        if (n == NULL_NOTE) continue;
        s32 dist;
        if (up) {
            if (n < pitchClass) n += OCTAVE;
            dist = n - pitchClass;
            orig = n;
        } else {
            if (n > pitchClass) n -= OCTAVE;
            dist = pitchClass - n;
        }
        if (dist < minDist) { minDist = dist; nearest = orig; }
    }
    if (nearest < 0) nearest = pitchClass;
    return (u8)(nearest + octave * OCTAVE);
}

static u8 getNearestChordNote(u8 note)
{
    if (isOnChord(note, 1, 1)) return note;
    u8 upNote = getNearestChordNoteDir(note, 1), downNote = getNearestChordNoteDir(note, 0);
    return upNote < downNote ? upNote : downNote;
}

static s32 getChordNoteIndex(u8 pitchClass)
{
    for (s32 i = 0; i < NUM_CHORD_NOTES; i++)
        if (ci.note_list[i] == pitchClass) return i;
    return -1;
}

static s32 addIndexInChordNoteList(s32 idx, s32 count, s32 *wrapCount)
{
    *wrapCount = 0;
    for (s32 c = 0; c < count; c++) {
        idx++;
        if (idx >= NUM_CHORD_NOTES) { (*wrapCount)++; idx = 0; }
        s32 skip = 0;
        while (ci.note_list[idx] == NULL_NOTE) {
            skip++; idx++;
            if (skip >= NUM_CHORD_NOTES) return -1;
            if (idx >= NUM_CHORD_NOTES) { (*wrapCount)++; idx = 0; }
        }
    }
    return idx;
}

static s32 subIndexInChordNoteList(s32 idx, s32 count, s32 *wrapCount)
{
    *wrapCount = 0;
    for (s32 c = 0; c < count; c++) {
        idx--;
        if (idx < 0) { (*wrapCount)--; idx = NUM_CHORD_NOTES - 1; }
        s32 skip = 0;
        while (ci.note_list[idx] == NULL_NOTE) {
            skip++; idx--;
            if (skip >= NUM_CHORD_NOTES) return -1;
            if (idx < 0) { (*wrapCount)--; idx = NUM_CHORD_NOTES - 1; }
        }
    }
    return idx;
}

static u8 getUpperNoteOnChord(u8 note, s32 steps)
{
    s32 n = note;
    if (!isOnChord(n, 1, 1)) { n = getNearestChordNoteDir(note, 1); steps--; }
    if (steps <= 0) return n;
    s32 octave = n / OCTAVE, wrapCount;
    s32 idx = addIndexInChordNoteList(getChordNoteIndex(n % OCTAVE), steps, &wrapCount);
    if (idx < 0) return n; /* (the Wii reads mChordNoteList[-1]) */
    wrapCount += octave;
    return ci.note_list[idx] + wrapCount * OCTAVE;
}

static u8 getLowerNoteOnChord(u8 note, s32 steps)
{
    s32 n = note;
    if (!isOnChord(n, 1, 1)) { n = getNearestChordNoteDir(note, 0); steps--; }
    if (steps <= 0) return n;
    s32 octave = n / OCTAVE, wrapCount;
    s32 idx = subIndexInChordNoteList(getChordNoteIndex(n % OCTAVE), steps, &wrapCount);
    if (idx < 0) return n;
    wrapCount += octave;
    if (wrapCount < 0) wrapCount = 0;
    return ci.note_list[idx] + wrapCount * OCTAVE;
}

static s32 getOnScaleType(u8 note)
{
    int onUp = 0, onDown = 0;
    s32 pitchClass = note % OCTAVE;
    for (s32 i = 0; i < OCTAVE; i++)
        if (pitchClass == getScaleNoteUp(i)) { onUp = 1; break; }
    for (s32 i = 0; i < OCTAVE; i++)
        if (pitchClass == getScaleNoteDown(i)) { onDown = 1; break; }
    if (onUp) return onDown ? ScaleType_UpDown : ScaleType_Up;
    return onDown ? ScaleType_Down : ScaleType_None;
}

static u8 getNearestScaleNoteDir(u8 note, int up)
{
    if (getOnScaleType(note) == (up ? ScaleType_Up : ScaleType_Down)) return note;
    s32 octave = note / OCTAVE, pitchClass = note % OCTAVE, minDist = 0x7FFF, nearest = -1;
    for (s32 i = 0; i < OCTAVE; i++) {
        s32 n = up ? getScaleNoteUp(i) : getScaleNoteDown(i), orig = n, dist;
        if (n == NULL_NOTE) continue;
        if (up) {
            if (n < pitchClass) n += OCTAVE;
            dist = n - pitchClass;
            orig = n;
        } else {
            if (n > pitchClass) n -= OCTAVE;
            dist = pitchClass - n;
        }
        if (dist < minDist) { minDist = dist; nearest = orig; }
    }
    if (nearest < 0) nearest = pitchClass;
    return (u8)(nearest + octave * OCTAVE);
}

static u8 getNearestScaleNote(u8 note)
{
    if (getOnScaleType(note) == ScaleType_UpDown) return note;
    u8 upNote = getNearestScaleNoteDir(note, 1), downNote = getNearestScaleNoteDir(note, 0);
    return upNote < downNote ? upNote : downNote;
}

static s32 getScaleNoteIndex(u8 pClass, int up)
{
    s32 pitchClass = pClass;
    for (s32 i = 0; i < OCTAVE; i++)
        if (pitchClass == (up ? getScaleNoteUp(i) : getScaleNoteDown(i))) return i;
    return -1;
}

static s32 addIndexInScaleNoteList(s32 idx, s32 count, s32 *wrapCount)
{
    s32 ret = idx;
    *wrapCount = 0;
    for (s32 c = 0; c < count; c++) {
        ret++;
        if (ret >= OCTAVE) { (*wrapCount)++; ret = 0; }
        s32 skip = 0;
        while (getScaleNoteUp(ret) == NULL_NOTE) {
            skip++; ret++;
            if (skip >= OCTAVE) return -1;
            if (ret >= OCTAVE) { (*wrapCount)++; ret = 0; }
        }
    }
    return ret;
}

static s32 subIndexInScaleNoteList(s32 idx, s32 count, s32 *wrapCount)
{
    s32 ret = idx;
    *wrapCount = 0;
    for (s32 c = 0; c < count; c++) {
        ret--;
        if (ret < 0) { (*wrapCount)--; ret = OCTAVE - 1; }
        s32 skip = 0;
        while (getScaleNoteDown(ret) == NULL_NOTE) {
            skip++; ret--;
            if (skip >= OCTAVE) return -1;
            if (ret < 0) { (*wrapCount)--; ret = OCTAVE - 1; }
        }
    }
    return ret;
}

static u8 getUpperNoteOnScale(u8 note, s32 steps)
{
    s32 n = note;
    if ((getOnScaleType(n) & 1) == 0) { n = getNearestScaleNoteDir(note, 1); steps--; }
    if (steps <= 0) return n;
    s32 octave = n / OCTAVE, wrapCount;
    s32 idx = addIndexInScaleNoteList(getScaleNoteIndex(n % OCTAVE, 1), steps, &wrapCount);
    if (idx < 0) return n;
    wrapCount += octave;
    return getScaleNoteUp(idx) + wrapCount * OCTAVE;
}

static u8 getLowerNoteOnScale(u8 note, s32 steps)
{
    s32 n = note;
    if ((getOnScaleType(n) & 2) == 0) { n = getNearestScaleNoteDir(note, 0); steps--; }
    if (steps <= 0) return n;
    s32 octave = n / OCTAVE, wrapCount;
    s32 idx = subIndexInScaleNoteList(getScaleNoteIndex(n % OCTAVE, 0), steps, &wrapCount);
    if (idx < 0) return n;
    wrapCount += octave;
    if (wrapCount < 0) wrapCount = 0;
    return getScaleNoteDown(idx) + wrapCount * OCTAVE;
}

int me_chord_notes(u8 out[8])
{
    memcpy(out, ci.note_list, 8);
    return ci_available();
}

/* ---- AudMeSeqReader / AudMeSeqCtrl ---- */
typedef struct {
    const u8 *buff, *cursor;
    u32 num_stacks;
    const u8 *stack[8];
    u16 loops[8];
} Reader;
typedef struct {
    s32 type, time; /* mType, mTime */
    Reader r;
    u8 locked, stop_after_note;
    s32 progress;
} SeqCtrl;

static void rd_init(Reader *r, const u8 *buf)
{
    memset(r, 0, sizeof *r);
    r->buff = r->cursor = buf;
}
static u32 rd8(Reader *r) { return *r->cursor++; }
static u32 rd16(Reader *r) { u32 v = rd8(r) << 8; return v | rd8(r); }
static u32 rd24(Reader *r) { u32 v = rd8(r) << 8 | rd8(r); return v << 8 | rd8(r); }
static int rd_call(Reader *r, u32 addr)
{
    if (r->num_stacks >= 8) return 0;
    r->stack[r->num_stacks++] = r->cursor;
    r->cursor = r->buff + addr;
    return 1;
}
static int rd_ret(Reader *r)
{
    if (r->num_stacks == 0) return 0;
    r->cursor = r->stack[--r->num_stacks];
    return 1;
}
static int rd_loop_start(Reader *r, u16 count)
{
    if (r->num_stacks >= 8) return 0;
    r->stack[r->num_stacks] = r->cursor;
    r->loops[r->num_stacks++] = count;
    return 1;
}
static int rd_loop_end(Reader *r)
{
    if (r->num_stacks == 0) return 0;
    u16 n = r->loops[r->num_stacks - 1];
    if (n > 0) n--;
    if (n == 0) { r->num_stacks--; return 1; }
    r->loops[r->num_stacks - 1] = n;
    r->cursor = r->stack[r->num_stacks - 1];
    return 1;
}

static void sc_init(SeqCtrl *c)
{
    memset(c, 0, sizeof *c);
    c->progress = -1;
    c->type = -1;
}
static void sc_wait(SeqCtrl *c, s32 type, s32 time) { c->type = type; c->time = time; }
static void sc_auto_wait(SeqCtrl *c)
{
    if (c->progress < 0) return;
    c->type = c->progress;
    c->time = 1;
}

/* ---- AudMeTrack ---- */
#define MAX_CHILDREN 8
#define TRACK_POOL 96 /* AudMeTrack::newMemPool(96) */
enum { STATUS_FREE, STATUS_READY, STATUS_RUN, STATUS_START };
enum { ModifyType_Chord, ModifyType_Scale, ModifyType_Steps, ModifyType_Octave };
enum { ModifyDir_Up, ModifyDir_Down, ModifyDir_None, ModifyDir_Rand };

typedef struct MeTrack MeTrack;
typedef struct { /* AudMeChannelMgr: one a track (mNumChannelMgrs 1) */
    u32 chan[8];                                   /* mChannels: se_note handles */
    float vol, pitch, pan, fxmix, dolby, bend;     /* mChannelParams (bend: _8, octaves) */
    float p_vol, p_pitch, p_pan, p_fxmix, p_dolby; /* mParams (JASSoundParams) */
} ChanMgr;
struct MeTrack {
    u8 used;
    SeqCtrl sc;
    MeTrack *parent, *child[MAX_CHILDREN];
    s32 brother;
    ChanMgr mgr;
    u8 proc_stopped;
    s32 status;
    u8 osc_over;  /* mOscParam[0].mTable set (setOscAdsr) */
    u8 adsr[24];  /* mOscPoint as big-endian {mode, time, value} */
    float volume, pitch_bend, fxmix, pan, dolby;
    u8 base_note, base_note_prev, in_start, stop_after_note, no_start_range;
    u32 me_id;
    s16 chord_table_id;
    u8 jump_flag;
    u8 _163[MAX_CHILDREN];
    struct { /* TTrackInfo */
        u8 velocity;
        u16 bank, prog;
        u8 bend_sense;
        u16 release;
        u8 play_prio, release_prio;
        u32 note_reg;
        u8 range_lower, range_upper, range_start;
    } info;
};
static MeTrack tpool[TRACK_POOL];
static MeTrack *tlist[TRACK_POOL]; /* AudMeTrack::sTrackList (front first) */
static int ntlist, tlist_registered;
static u8 sReg[64];

static void tlist_remove(MeTrack *t)
{
    for (int i = 0; i < ntlist; i++)
        if (tlist[i] == t) { memmove(tlist + i, tlist + i + 1, (ntlist - i - 1) * sizeof *tlist); ntlist--; return; }
}

static void initRegister(void)
{
    for (int i = 0; i < 64; i++) sReg[i] = 60;
}

static void tlist_append(MeTrack *t) /* TList::append: Push_front, the callback registered the first time */
{
    if (!tlist_registered) { initRegister(); tlist_registered = 1; }
    if (ntlist >= TRACK_POOL) return;
    memmove(tlist + 1, tlist, ntlist * sizeof *tlist);
    tlist[0] = t;
    ntlist++;
}

static void mgr_init(ChanMgr *m)
{
    memset(m, 0, sizeof *m);
    m->vol = m->pitch = 1.0f; m->pan = 0.5f;            /* JASChannelParams */
    m->p_vol = m->p_pitch = 1.0f; m->p_pan = 0.5f;      /* JASSoundParams::init */
}
static int mgr_note_off(ChanMgr *m, u32 n, u16 dr) /* AudMeChannelMgr::noteOff */
{
    u32 h = m->chan[n];
    if (!h) return 0;
    se_note_off(h, dr);
    m->chan[n] = 0;
    return 1;
}

static const u8 sAdsTable[24] = {0, 0, 0, 0, 0x7F, 0xFF, 0, 0, 0, 0, 0x7F, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0xE, 0, 0, 0, 0};

static void track_init(MeTrack *t) /* AudMeTrack() + init() */
{
    memset(t, 0, sizeof *t);
    t->used = 1;
    sc_init(&t->sc);
    mgr_init(&t->mgr);
    t->in_start = 1;
    t->chord_table_id = -1;
    t->brother = -1;
    t->info.velocity = 100; t->info.bank = 0; t->info.prog = 0xF0; t->info.bend_sense = 12; t->info.play_prio = 64;
    t->info.range_lower = 0; t->info.range_upper = 12 * 10; t->info.range_start = 12 * 5;
    t->volume = 1.0f; t->pan = 0.5f; /* initPlayParam */
    t->base_note = t->base_note_prev = 60;
    memcpy(t->adsr, sAdsTable, sizeof t->adsr);
    t->status = STATUS_FREE;
}
static MeTrack *track_new(void)
{
    for (int i = 0; i < TRACK_POOL; i++)
        if (!tpool[i].used) { track_init(&tpool[i]); return &tpool[i]; }
    return NULL;
}

static void noteOff(MeTrack *t, u32 n, u16 dr) { mgr_note_off(&t->mgr, n, dr); }
static void noteOffAll(MeTrack *t, u16 dr)
{
    for (u8 i = 0; i < MAX_CHILDREN; i++) noteOff(t, i, dr);
}

static void track_delete(MeTrack *t) /* ~AudMeTrack */
{
    if (!t->parent) tlist_remove(t);
    noteOffAll(t, 0);
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) track_delete(t->child[i]);
    t->used = 0;
}

static void inherit(MeTrack *t, MeTrack *p)
{
    t->info = p->info;
    t->osc_over = p->osc_over;
    memcpy(t->adsr, p->adsr, sizeof t->adsr);
    t->sc.progress = p->sc.progress; /* AudMeSeqCtrl::inherit */
    sc_auto_wait(&t->sc);
    t->stop_after_note = p->stop_after_note;
    if (t->stop_after_note) t->sc.stop_after_note = 1;
    t->jump_flag = p->jump_flag;
    memcpy(t->_163, p->_163, sizeof t->_163);
}

static MeTrack *newChild(MeTrack *t, s32 n)
{
    if (t->child[n]) return NULL;
    MeTrack *c = t->child[n] = track_new();
    if (!c) return NULL;
    inherit(c, t);
    c->parent = t;
    c->brother = n;
    return c;
}
static int deleteChild(MeTrack *t, s32 n)
{
    if (!t->child[n]) return 0;
    noteOffAll(t, 0);
    track_delete(t->child[n]);
    t->child[n] = NULL;
    return 1;
}
static int isHaveChild(const MeTrack *t)
{
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) return 1;
    return 0;
}

static void setSeqData(MeTrack *t, const u8 *data, u32 addr)
{
    t->sc.locked = 0; /* AudMeSeqCtrl::start */
    rd_init(&t->sc.r, data);
    t->sc.r.cursor = data + addr;
    t->me_id = addr;
    t->status = STATUS_READY;
}
static void startSeq(MeTrack *t)
{
    tlist_append(t);
    t->status = STATUS_RUN;
}
static void stopSeq(MeTrack *t)
{
    noteOffAll(t, 0);
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) noteOffAll(t->child[i], 0);
    tlist_remove(t);
    t->status = STATUS_FREE;
}
static void suspendSeq(MeTrack *t)
{
    noteOffAll(t, 0);
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) noteOffAll(t->child[i], 0);
    t->status = STATUS_FREE;
}
static void stopAtCurrentNoteEnd(MeTrack *t)
{
    t->stop_after_note = 1;
    t->sc.stop_after_note = 1;
    if (t->parent) return;
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) stopAtCurrentNoteEnd(t->child[i]);
}

static u32 channelStart(MeTrack *t, ChanMgr *m, u32 pitch, u32 vel)
{
    u32 h = se_note(t->info.bank, t->info.prog, (int)pitch, (int)vel, t->osc_over ? t->adsr : NULL, t->info.release);
    if (!h) return 0;
    se_note_params(h, m->vol, m->pitch, m->pan, m->bend); /* setParams */
    return h;
}
static int noteOn(MeTrack *t, u32 n, u32 pitch, u32 vel)
{
    mgr_note_off(&t->mgr, n, 0);
    u32 h = channelStart(t, &t->mgr, pitch, vel);
    t->mgr.chan[n] = h;
#ifdef SE_LOG
    printf("me: note %u vel %u bank %u prog %u -> %u\n", pitch, vel, t->info.bank, t->info.prog, h);
#endif
    return h != 0;
}

static void setOscAdsr(MeTrack *t, s16 a, s16 d, s16 s, s16 sl, u16 release)
{
    t->osc_over = 1; /* mOscParam[Env] = sEnvOsc with mOscPoint as its table */
    u16 tv[3] = {(u16)a, (u16)d, (u16)s};
    for (int k = 0; k < 3; k++) { t->adsr[6 * k + 2] = tv[k] >> 8; t->adsr[6 * k + 3] = (u8)tv[k]; }
    t->adsr[16] = (u16)sl >> 8; t->adsr[17] = (u8)sl;
    t->info.release = release;
}

static void updateChannelParams(MeTrack *t)
{
    ChanMgr *m = &t->mgr;
    float vol = t->volume * t->volume, pitch = 1.0f, bend = t->pitch_bend * t->info.bend_sense * (1.0f / 3.0f);
    float pan = t->pan - 0.5f, dolby = t->dolby, fxmix = t->fxmix;
    vol *= m->p_vol;
    pitch *= m->p_pitch;
    pan += m->p_pan - 0.5f;
    fxmix += m->p_fxmix;
    dolby += m->p_dolby;
    if (!t->parent) {
        m->vol = vol; m->pitch = pitch; m->pan = pan; m->fxmix = fxmix; m->dolby = dolby; m->bend = bend;
    } else {
        const ChanMgr *pm = &t->parent->mgr;
        m->vol = pm->vol * vol;
        m->pitch = pm->pitch * pitch;
        m->pan = (pm->pan - 0.5f) + pan;
        m->fxmix = pm->fxmix + fxmix;
        m->dolby = pm->dolby + dolby;
        m->bend = pm->bend + bend;
    }
    m->pan += 0.5f;
    for (int i = 0; i < 8; i++) /* channelUpdateCallback CB_PLAY: the channels follow; CB_STOP: an ended one dropped */
        if (m->chan[i]) {
            if (!se_note_alive(m->chan[i])) m->chan[i] = 0;
            else se_note_params(m->chan[i], m->vol, m->pitch, m->pan, m->bend);
        }
}

static void gframeProc(MeTrack *t)
{
    updateChannelParams(t);
    if (t->parent) return;
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) updateChannelParams(t->child[i]);
}

static u32 rnd_seed; /* AudMeTrack::getRandom: JMath::TRandom_fast_ oRandom(0) */
static u32 getRandom(u32 range)
{
    rnd_seed = 0x19660d * rnd_seed + 0x3c6ef35f;
    return (rnd_seed >> 9) % range;
}
static void makeRandomSearchList(s32 *list, s32 n, int ordered)
{
    for (s32 i = 0; i < n; i++) list[i] = i;
    if (!ordered)
        for (u32 i = 0; i < (u32)n; i++) {
            s32 a = getRandom(n), b = getRandom(n), tmp = list[a];
            list[a] = list[b];
            list[b] = tmp;
        }
}

static void adjustInNoteRange(MeTrack *t, u8 *note)
{
    while (*note > t->info.range_upper) *note -= 12;
    while (*note < t->info.range_lower) *note += 12;
}
static void adjustInStartNoteRange(MeTrack *t, u8 *note)
{
    if (!t->in_start || t->no_start_range) return;
    while (*note > t->info.range_start + 12) *note -= 12;
    while (*note < t->info.range_start) *note += 12;
}

static MeTrack *getSameME(MeTrack *t, u32 id) /* TList::getSameME */
{
    for (int i = 0; i < ntlist; i++)
        if (tlist[i] != t && tlist[i]->me_id == id) return tlist[i];
    return NULL;
}

static void startNote(MeTrack *t)
{
    noteOffAll(t, 0);
    if (t->base_note != NULL_NOTE) {
        adjustInStartNoteRange(t, &t->base_note);
        adjustInNoteRange(t, &t->base_note);
        noteOn(t, 1, t->base_note, t->info.velocity);
        sReg[t->info.note_reg & 63] = t->base_note;
    }
    t->base_note_prev = t->base_note;
    if (t->in_start) {
        MeTrack *p = t->parent;
        if (p) {
            MeTrack *same = getSameME(p, p->me_id);
            if (same) suspendSeq(same);
            p->status = STATUS_START;
        }
        t->status = STATUS_START;
    }
    t->in_start = 0;
}

static void setBaseNoteFromChord(MeTrack *t, const u8 *data, s32 n, int random)
{
    if (!ci_available()) { t->base_note = NULL_NOTE; return; }
    s32 ctrlList[16], chordNotes[8];
    makeRandomSearchList(ctrlList, n, random == 0);
    makeRandomSearchList(chordNotes, 8, 0); /* always randomized */
    for (s32 i = 0; i < n; i++) {
        u8 ctrl = data[ctrlList[i]];
        for (s32 j = 0; j < 8; j++) {
            u8 c = ci.note_list[chordNotes[j]];
            if (c == NULL_NOTE) continue;
            if (((ctrl & 0x01) && c == getBassNote()) || ((ctrl & 0x02) && c == getRoot()) ||
                ((ctrl & 0x04) && c == getThird()) || ((ctrl & 0x08) && c == getFifth()) ||
                ((ctrl & 0x10) && c == getSixth()) || ((ctrl & 0x20) && c == getSeventh()) ||
                ((ctrl & 0x40) && c == getNinth()) || ((ctrl & 0x80) && c == getEleventh())) {
                t->base_note = c;
                return;
            }
        }
    }
    t->base_note = NULL_NOTE;
}
static void setBaseNoteFromScale(MeTrack *t, const u8 *data, s32 n, int random)
{
    if (!ci_available()) { t->base_note = NULL_NOTE; return; }
    s32 ctrlList[16];
    makeRandomSearchList(ctrlList, n, random == 0);
    for (s32 i = 0; i < n; i++) {
        u8 ctrl = data[ctrlList[i]];
        u8 note = (ctrl & 0x80) ? ci.up[ctrl & 0x7F] : ci.down[ctrl];
        if (note != NULL_NOTE) { t->base_note = note; return; }
    }
    t->base_note = NULL_NOTE;
}
static void setBaseNoteFromChromatic(MeTrack *t, const u8 *data, s32 n, int random)
{
    if (!ci_available()) { t->base_note = NULL_NOTE; return; }
    s32 ctrlList[12];
    makeRandomSearchList(ctrlList, n, random == 0);
    t->base_note = data[ctrlList[0]];
}
static MeTrack *getBrother(MeTrack *t, s32 n) { return t->parent ? t->parent->child[n] : NULL; }

static void adjustNote(MeTrack *t, s32 dir, s32 type)
{
    if (t->base_note == NULL_NOTE || !ci_available()) return;
    u8 note = t->base_note;
    if (type == ModifyType_Chord)
        note = dir == ModifyDir_Up ? getNearestChordNoteDir(note, 1) : dir == ModifyDir_Down ? getNearestChordNoteDir(note, 0)
             : dir == ModifyDir_None ? getNearestChordNote(note) : note;
    else if (type == ModifyType_Scale)
        note = dir == ModifyDir_Up ? getNearestScaleNoteDir(note, 1) : dir == ModifyDir_Down ? getNearestScaleNoteDir(note, 0)
             : dir == ModifyDir_None ? getNearestScaleNote(note) : note;
    t->base_note = note;
}

static void modifyNote(MeTrack *t, s32 dir, s32 type, s32 steps)
{
    if (t->base_note == NULL_NOTE) return;
    if (dir == ModifyDir_Rand) dir = getRandom(2) != 0;
    if (!ci_available()) return;
    switch (type) {
    case ModifyType_Chord:
        if (dir == ModifyDir_Up) t->base_note = getUpperNoteOnChord(t->base_note, steps);
        else if (dir == ModifyDir_Down) t->base_note = getLowerNoteOnChord(t->base_note, steps);
        break;
    case ModifyType_Scale:
        if (dir == ModifyDir_Up) t->base_note = getUpperNoteOnScale(t->base_note, steps);
        else if (dir == ModifyDir_Down) t->base_note = getLowerNoteOnScale(t->base_note, steps);
        break;
    case ModifyType_Steps: t->base_note += dir == ModifyDir_Up ? steps : -steps; break;
    case ModifyType_Octave: t->base_note += dir == ModifyDir_Up ? steps * 12 : -steps * 12; break;
    }
    if (t->base_note != NULL_NOTE) adjustInNoteRange(t, &t->base_note);
}

/* ---- AudMeSeqParser ---- */
static const u8 cmd_args[0xA0][6] = { /* {count, widths...}: 0 u8, 1 u16, 2 u24 */
    [0x00] = {1, 0}, [0x01] = {1, 0}, [0x02] = {1, 1}, [0x03] = {1, 1}, [0x04] = {5, 1, 1, 1, 1, 1},
    [0x10] = {2, 0, 2}, [0x11] = {1, 0}, [0x20] = {1, 0}, [0x28] = {2, 0, 1}, [0x2A] = {1, 0},
    [0x30] = {1, 2}, [0x31] = {1, 2}, [0x33] = {1, 1}, [0x35] = {1, 2}, [0x36] = {1, 2}, [0x3A] = {1, 2}, [0x3B] = {1, 2},
    [0x40] = {2, 2, 0}, [0x41] = {3, 2, 0, 0}, [0x42] = {3, 2, 0, 0}, [0x43] = {4, 2, 0, 0, 0},
    [0x50] = {1, 0}, [0x51] = {1, 0}, [0x52] = {1, 0}, [0x53] = {1, 0}, [0x54] = {1, 0},
    [0x58] = {1, 0}, [0x59] = {1, 0}, [0x5A] = {1, 1}, [0x5B] = {1, 1}, [0x5C] = {1, 0}, [0x5D] = {1, 1}, [0x60] = {1, 0},
    [0x68] = {2, 0, 0}, [0x69] = {2, 0, 0}, [0x78] = {1, 2}, [0x79] = {1, 2},
    [0x81] = {1, 2}, [0x82] = {1, 2}, [0x83] = {1, 2}, [0x84] = {1, 0}, [0x86] = {1, 0}, [0x87] = {1, 0},
    [0x8A] = {2, 0, 0}, [0x90] = {3, 0, 0, 0}, [0x91] = {3, 0, 0, 0}};

static s32 parse(MeTrack *t) /* AudMeSeqParser::parse and its commands */
{
    SeqCtrl *sc = &t->sc;
    u32 op = rd8(&sc->r), a[8];
    if (op >= 0xA0) return 0;
    for (int i = 0; i < cmd_args[op][0]; i++) {
        int w = cmd_args[op][1 + i];
        a[i] = w == 0 ? rd8(&sc->r) : w == 1 ? rd16(&sc->r) : rd24(&sc->r);
    }
    switch (op) {
    case 0x00: t->info.bank = a[0]; return 0;
    case 0x01: t->info.prog = a[0]; return 0;
    case 0x02: t->info.bank = (u8)(a[0] >> 8); t->info.prog = (u8)a[0]; return 0;
    case 0x03: t->info.release = a[0]; return 0;
    case 0x04: setOscAdsr(t, a[0], a[1], a[2], a[3], a[4]); return 0;
    case 0x10: {
        MeTrack *c = newChild(t, a[0] & 7);
        if (c) setSeqData(c, sc->r.buff, a[1]);
        return 0;
    }
    case 0x11: deleteChild(t, a[0] & 7); return 0;
    case 0x20: if (a[0] != 6) sc_wait(sc, a[0], 1); return 0;
    case 0x24: return -1;
    case 0x28: sc_wait(sc, a[0], a[1]); return 0;
    case 0x2A: sc->progress = a[0]; return 0;
    case 0x2B: sc->progress = -1; return 0;
    case 0x30: sc->r.cursor = sc->r.buff + a[0]; return 0;
    case 0x31: rd_call(&sc->r, a[0]); return 0;
    case 0x32: rd_ret(&sc->r); return 0;
    case 0x33: rd_loop_start(&sc->r, a[0]); return 0;
    case 0x34: rd_loop_end(&sc->r); return 0;
    case 0x35: if (t->jump_flag == 0) sc->r.cursor = sc->r.buff + a[0]; t->jump_flag = 0; return 0;
    case 0x36: if (t->jump_flag != 0) sc->r.cursor = sc->r.buff + a[0]; t->jump_flag = 0; return 0;
    case 0x38: sc->locked = 1; return 0;
    case 0x39: sc->type = -1; sc->time = 0; return 0;
    case 0x3A: if (t->jump_flag == 0) rd_call(&sc->r, a[0]); t->jump_flag = 0; return 0;
    case 0x3B: if (t->jump_flag != 0) rd_call(&sc->r, a[0]); t->jump_flag = 0; return 0;
    case 0x40: t->info.velocity = a[1]; rd_call(&sc->r, a[0]); return 1;
    case 0x48: noteOffAll(t, 0); return 1;
    case 0x49: sc_auto_wait(sc); return 0;
    case 0x4A: noteOffAll(t, 0); sc_auto_wait(sc); return 1;
    case 0x50: t->info.range_upper = a[0]; return 0;
    case 0x51: t->info.range_lower = a[0]; return 0;
    case 0x52: t->info.range_start = a[0]; return 0;
    case 0x53: t->info.range_start = 0x7F; return 0;
    case 0x54: t->info.range_start = 0x7E; return 0;
    case 0x55: t->no_start_range = 1; return 0;
    case 0x58: t->pan = a[0] / 127.0f; return 0;
    case 0x59: t->dolby = a[0] / 127.0f; return 0;
    case 0x5A: t->volume = a[0] / 32767.0f; return 0;
    case 0x5B: t->pitch_bend = (s16)a[0] / 32767.0f; return 0;
    case 0x5C: t->info.bend_sense = a[0]; return 0;
    case 0x5D: t->fxmix = a[0] / 32767.0f; return 0;
    case 0x60: t->info.note_reg = a[0]; return 0;
    case 0x68: t->jump_flag = (u8)a[0] == sReg[a[0] & 63]; return 0; /* cmdCmpNI (as Petari has it) */
    case 0x69: t->jump_flag = (u8)a[0] == t->_163[a[0] & 7]; return 0; /* cmdCmpCI */
    case 0x70: noteOffAll(t, 0); return 1;
    case 0x71: startNote(t); sc_auto_wait(sc); rd_ret(&sc->r); return 0;
    case 0x80: t->base_note = t->base_note_prev; return 0;
    case 0x81: { const u8 *d = sc->r.buff + a[0]; setBaseNoteFromChord(t, d + 2, d[0], d[1] != 0); return 0; }
    case 0x82: { const u8 *d = sc->r.buff + a[0]; setBaseNoteFromScale(t, d + 2, d[0], d[1] != 0); return 0; }
    case 0x83: { const u8 *d = sc->r.buff + a[0]; setBaseNoteFromChromatic(t, d + 2, d[0], d[1] != 0); return 0; }
    case 0x84: { MeTrack *b = getBrother(t, a[0] & 7); if (b) t->base_note = b->base_note; return 0; }
    case 0x85: { MeTrack *b = t->parent && t->brother > 0 ? getBrother(t, t->brother - 1) : NULL; if (b) t->base_note = b->base_note; return 0; }
    case 0x86: t->base_note = a[0]; return 0;
    case 0x87: t->base_note = sReg[a[0] & 63]; return 0;
    case 0x8A: adjustNote(t, a[0], a[1]); return 0;
    case 0x90: modifyNote(t, (u8)a[0], (u8)a[1], (u8)a[2]); return 0;
    case 0x91: modifyNote(t, (u8)a[0], (u8)a[1], getRandom((u8)a[2])); return 0; /* modifyNoteRnd */
    }
    return 0; /* 0x41-0x43, 0x78 / 0x79: nothing (as Petari has them) */
}

static s32 sc_rhythm_proc(MeTrack *t, s32 type) /* AudMeSeqCtrl::rhythmProc */
{
    SeqCtrl *sc = &t->sc;
    if (!sc->r.buff) return 0;
    if (sc->type == type && sc->time > 0 && --sc->time == 0) { sc->type = -1; sc->time = 0; }
    if (sc->time > 0) return 0;
    for (int guard = 0; guard < 4096 && sc->time == 0 && !sc->locked; guard++) {
        s32 ret = parse(t);
        if (ret < 0) return -1;
        if (ret == 1 && sc->stop_after_note) return 1;
    }
    return 0;
}

static s32 track_rhythm_proc(MeTrack *t, s32 type) /* AudMeTrack::rhythmProc */
{
    if (!t->parent && t->status == STATUS_FREE) return -1;
    updateChannelParams(t);
    if (t->parent) return 0;
    t->chord_table_id = ci.table_id;
    if (!t->proc_stopped) {
        s32 ret = sc_rhythm_proc(t, type);
        if ((ret == 1 && t->stop_after_note) || ret < 0) t->proc_stopped = 1;
    }
    for (s32 i = 0; i < MAX_CHILDREN; i++)
        if (t->child[i]) {
            s32 ret = sc_rhythm_proc(t->child[i], type);
            if ((ret == 1 && t->stop_after_note) || ret < 0) deleteChild(t, i);
        }
    if (!isHaveChild(t) && t->proc_stopped) {
        t->status = STATUS_FREE;
        return -1;
    }
    return 0;
}

static void seqMain(s32 type) /* AudMeTrackCallback -> TList::seqMain */
{
    if (!tlist_registered) return;
    for (int i = 0; i < ntlist;) {
        MeTrack *t = tlist[i];
        if (track_rhythm_proc(t, type) < 0) tlist_remove(t);
        else i++;
    }
}

/* ---- AudMe / AudMeMgr / AudMeObject ---- */
#define ME_POOL 32 /* AudMe::newMemPool(32) */
enum { MoveParam_Volume, MoveParam_Pitch, MoveParam_FxMix, MoveParam_Pan, MoveParam_Dolby, MoveParam_DistFilter };
typedef struct Me Me;
typedef struct { Me *me; } MeHandle; /* AudMeHandle */
struct Me {
    u8 used;
    MeHandle *handle;
    MeTrack *track;
    s32 me_id, stop_time; /* mMeId, _24 */
    u32 timer;
    struct { float value, target, delta; } mp[6];
    s32 start_req; /* _74 */
    const u8 *data;
    u32 start_pos; /* _7C */
    float _80, volume, _88, _8C;
    s32 priority, _94;
};
static Me mpool[ME_POOL];
static Me *melist[ME_POOL]; /* AudMeMgr::mMeList (appended) */
static int nmelist;
static MeHandle sys_handles[8]; /* AudSystem::mSystemMeObject: AudMeObject(nullptr, 8) */
static const u8 *me_table, *me_seq;
static u32 me_entries;

static void me_release_handle(Me *m)
{
    if (!m->handle) return;
    m->handle->me = NULL;
    m->handle = NULL;
}

static void me_done_stop(Me *m);
static void me_stop(Me *m, s32 time)
{
    if (time == -1) { if (m->track) stopAtCurrentNoteEnd(m->track); return; }
    if (time == -2) { me_done_stop(m); return; }
    if (time == 0) time = m->_94;
    if (time != 0) { /* setTimedParam(DistFilter, 0, time) */
        m->mp[MoveParam_DistFilter].delta = (0.0f - m->mp[MoveParam_DistFilter].value) / time;
        m->mp[MoveParam_DistFilter].target = 0.0f;
        return;
    }
    me_done_stop(m);
}

static void me_release(Me *m)
{
    if (m->track) {
        if (m->track->status != STATUS_FREE) me_stop(m, 0);
        track_delete(m->track);
        m->track = NULL;
        m->me_id = -1;
    }
    me_release_handle(m);
}

static void me_done_stop(Me *m)
{
    if (m->track) {
        stopSeq(m->track);
        me_release(m);
    }
    me_release_handle(m);
    m->stop_time = -1;
}

static void updateTimedParams(Me *m, int immediate)
{
    if (!immediate)
        for (int i = 0; i < 6; i++)
            if (m->mp[i].delta != 0.0f) {
                m->mp[i].value += m->mp[i].delta;
                if (m->mp[i].delta > 0.0f ? m->mp[i].value >= m->mp[i].target : m->mp[i].value <= m->mp[i].target) {
                    m->mp[i].value = m->mp[i].target;
                    m->mp[i].delta = 0.0f;
                }
            }
    if (!m->track) return;
    ChanMgr *c = &m->track->mgr;
    c->p_vol = m->_80 * (m->volume * (m->mp[MoveParam_Volume].value * m->mp[MoveParam_DistFilter].value));
    c->p_fxmix = m->mp[MoveParam_FxMix].value;
    c->p_pitch = m->mp[MoveParam_Pitch].value;
    c->p_pan = (m->mp[MoveParam_Pan].value - 0.5f) + (m->_88 - 0.5f) + 0.5f;
    c->p_dolby = m->mp[MoveParam_Dolby].value + m->_8C;
}

static void me_start_track(Me *m) /* AudMe::start */
{
    m->track = track_new();
    if (!m->track) return;
    setSeqData(m->track, m->data, m->start_pos);
    updateTimedParams(m, 1);
    startSeq(m->track);
}

static void me_update_one(Me *m) /* AudMe::update */
{
    if (m->start_req == 1) { me_start_track(m); m->start_req = 0; }
    if (m->stop_time > 0 && --m->stop_time <= 0) { me_stop(m, 0); m->stop_time = -1; }
    if (m->mp[MoveParam_DistFilter].value <= 0.0f) me_done_stop(m);
    if (m->track) {
        updateTimedParams(m, 0);
        gframeProc(m->track);
        if (m->track->status == STATUS_FREE) { track_delete(m->track); m->track = NULL; }
    }
    m->timer++;
}

static Me *me_new(void)
{
    for (int i = 0; i < ME_POOL; i++)
        if (!mpool[i].used) {
            Me *m = &mpool[i];
            memset(m, 0, sizeof *m);
            m->used = 1; m->me_id = -1; m->stop_time = -1; m->_80 = 1.0f;
            m->mp[MoveParam_Volume].value = m->mp[MoveParam_Pitch].value = 1.0f; /* initTimed */
            m->mp[MoveParam_Pan].value = 0.5f;
            m->mp[MoveParam_DistFilter].value = 1.0f;
            for (int k = 0; k < 6; k++) m->mp[k].target = m->mp[k].value;
            return m;
        }
    return NULL;
}
static void me_delete(Me *m) { me_release(m); m->used = 0; }

static u8 me_priority(u32 id) { return me_table[be32(me_table + 4) + 8 * id]; }

static int isRequestedMe(u32 pos)
{
    for (int i = 0; i < nmelist; i++) {
        MeTrack *t = melist[i]->track;
        if (t && pos == t->me_id && (t->status == STATUS_RUN || t->status == STATUS_READY)) return 1;
    }
    return 0;
}

static int mgr_start_me(u32 id, MeHandle *h) /* AudMeMgr::startMe */
{
    if (id == (u32)-1 || !me_seq || !ci_available()) return 0;
    if (h && h->me && h->me->stop_time > -1) me_stop(h->me, 0);
    Me *m = me_new();
    if (!m) return 0;
    if (h) { /* attachHandle */
        m->handle = h;
        h->me = m;
    }
    s32 pos = id < be32(me_seq) ? (s32)be32(me_seq + 4 + 4 * id) : -1; /* getSeqStartPos */
    if (pos < 0 || isRequestedMe(pos)) {
        if (h) h->me = NULL;
        m->handle = NULL;
        me_delete(m);
        return 0;
    }
    m->me_id = id;
    const u8 *p = me_table + be32(me_table + 4) + 8 * id; /* setOuterPlayingParam: AudMePlayingParams */
    float vol = p[1] / 127.0f;
    m->volume = vol * vol; m->_88 = p[2] / 127.0f; m->_8C = p[3] / 127.0f; m->priority = p[0]; m->_94 = be16(p + 4);
    m->data = me_seq; m->start_pos = pos; m->start_req = 1; /* requestStart */
    if (nmelist < ME_POOL) melist[nmelist++] = m;
    m->_80 = 1.0f; /* AudMeMgr::_50 */
    return 1;
}

static void obj_start_me(u32 id) /* AudMeObject::startMe on the system object */
{
    if (id == (u32)-1) return;
    MeHandle *h = NULL;
    for (int i = 0; i < 8 && !h; i++) /* getHandleMeID */
        if (sys_handles[i].me && sys_handles[i].me->me_id == (s32)id) h = &sys_handles[i];
    if (h && h->me && h->me->timer == 0) return;
    for (int i = 0; i < 8 && !h; i++) /* getFreeHandle */
        if (!sys_handles[i].me) h = &sys_handles[i];
    if (!h) { /* getLowestPrioHandle */
        u8 lowest = 0xFF;
        MeHandle *lh = NULL;
        for (int i = 0; i < 8 && !h; i++) {
            Me *m = sys_handles[i].me;
            if (!m || m->me_id == -1) { h = &sys_handles[i]; break; }
            u8 pr = me_priority(m->me_id);
            if (pr < lowest) { lowest = pr; lh = &sys_handles[i]; }
        }
        if (!h && me_priority(id) >= lowest) h = lh;
    }
    if (h) mgr_start_me(id, h);
}

void me_set_resource(const u8 *table, const u8 *seq)
{
    me_table = table;
    me_seq = seq;
    me_entries = table ? be32(table) : 0;
}

int me_has(void) { return ci_available(); }

void me_start_system(const char *name)
{
    if (!me_has()) return;
    u32 id = (u32)-1;
    for (u32 i = 0; i < me_entries && id == (u32)-1; i++) /* AudMeNameConverter::getMeID */
        if (!strcmp((const char *)me_table + be32(me_table + be32(me_table + 8) + 4 * i), name)) id = i;
#ifdef SE_LOG
    printf("me: start %s (%d)\n", name, (int)id);
#endif
    obj_start_me(id);
}

void me_start_random(const char *stem, int n)
{
    char name[48];
    snprintf(name, sizeof name, "%s%d", stem, 1 + rand() % n); /* MR::getRandom(0, n) */
    me_start_system(name);
}

void me_update(void) /* AudMeMgr::update: updateEachMe, freeDeadMe */
{
    for (int i = 0; i < nmelist; i++) {
        if (!ci_available()) me_stop(melist[i], 0);
        me_update_one(melist[i]);
    }
    for (int i = 0; i < nmelist;)
        if (!melist[i]->track) {
            me_delete(melist[i]);
            memmove(melist + i, melist + i + 1, (nmelist - i - 1) * sizeof *melist);
            nmelist--;
        } else i++;
}

void me_reset(void)
{
    for (int i = 0; i < nmelist; i++) me_stop(melist[i], -2);
    for (int i = 0; i < nmelist; i++) me_delete(melist[i]);
    nmelist = 0;
    me_rhythm(NULL, 0);
    me_set_resource(NULL, NULL);
}

/* ---- the rhythm (AudRhythmSeqParser's callbacks, from the .rhy) ---- */
static const u8 *rhy;
static const u32 *rev;
static u32 nrev, rnext, rlast;
static int rfirst;

void me_rhythm(const u8 *b, u32 size)
{
    rhy = NULL;
    ci_init_params(); /* AudBgmRhythmStrategy::reject: invalidiate */
    ci.res = NULL;
    if (!b || size < 24 || memcmp(b, "SMGR", 4)) return;
    const u32 *h = (const u32 *)(b + 4);
    if (h[1] + h[2] > size || h[3] + 4 * h[4] > size) return;
    rhy = b;
    rev = (const u32 *)(b + h[3]);
    nrev = h[4];
    rnext = 0; rlast = 0; rfirst = 1;
    loadChordInfo((s16)h[0], b + h[1]); /* setDominant: loadChordInfo(getChordResIdBgm, false) */
#ifdef SE_LOG
    printf("me: rhythm, chord table %u, %u events\n", h[0], nrev);
#endif
}

static void rhythm_event(u32 e, int callbacks)
{
    u32 kind = e >> 8 & 3, v = e & 0xFF;
    if (kind == 0) { if (callbacks) seqMain((s32)v - 1); } /* AudMeTrackCallback::callback(type) */
    else if (kind == 1) { if (ci_available()) setCurChord(v); } /* registChordNumToInfo */
    else if (ci_available()) setCurScale(v);                    /* registScaleNumToInfo */
}

void me_rhythm_tick(u32 pos, u32 loop)
{
    if (!rhy || !nrev) return;
    u32 at = pos / 4 / 2; /* stream bytes -> the events' half samples */
    if (rfirst) { /* started mid-way (a state file at its place): the chord / scale there, no callbacks */
        while (rnext < nrev && rev[rnext] >> 10 <= at) rhythm_event(rev[rnext++], 0);
        rfirst = 0; rlast = at;
        return;
    }
    if (at < rlast) { /* the stream went round its loop: the events to its end, then those from its loop start */
        while (rnext < nrev) rhythm_event(rev[rnext++], 1);
        u32 from = loop == 0xFFFFFFFFu ? 0 : loop / 4 / 2;
        rnext = 0;
        while (rnext < nrev && rev[rnext] >> 10 < from) rnext++;
    }
    while (rnext < nrev && rev[rnext] >> 10 <= at) rhythm_event(rev[rnext++], 1);
    rlast = at;
}
