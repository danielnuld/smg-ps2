"""The rhythm BGMs' beat / chord timelines for the ME sequencer (engine/src/me.c): <stream>.rhy beside the music.

On the Wii a rhythm BGM (sound table flag 1: every stage BGM, the power-up themes) runs its BMS sequence beside the
stream (AudMultiBgm: cMultiBgmSet's BGM_<id> sequence + STM_<id> stream, kept in step by AudBgmTempoAdjuster) or as
the music itself (the sequenced ones); its root track and track 0's track 0 use AudRhythmSeqParser, whose MIDI note-ons
call the ME tracks back (AudMeTrackCallback: key 0x18 / 0x1E / 0x1B / 0x19 / 0x1A -> rhythm types 0-4, any other key
-1, type 0 under velocity 110 type 5 first) and whose bank / program commands (0xE2 / 0xE3) set the chord table's
scale / chord (registScaleNumToInfo / registChordNumToInfo). The PS2 plays the music as PCM (tools/music.py,
bgmseq.py), so the rhythm track is run here and its events kept against the stream's samples (32 kHz).
ponytail: the sequence at its nominal tempo (no AudBgmTempoAdjuster correction: the stream and the sequence agree
by construction; the adjuster's 500-sample lead not kept); gate notes start no channel (their waits do not hang on one).

.rhy (little-endian): 'SMGR', u32 chord resource id (JaiChord.arc), u32 chord table offset, u32 its size, u32 events
offset, u32 events; the chord table (the .cit as on disc, big-endian); events u32 {stream sample / 2 << 10 | kind << 8
| value} in time order, kind 0 a rhythm callback (value = type + 1), 1 the chord, 2 the scale.

usage: python tools/rhythm.py data/fs/files out/music      |  python tools/rhythm.py --selftest [data/fs/files]
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bgmseq as Q  # noqa: E402
import sound as S  # noqa: E402
from budget import rarc_files  # noqa: E402
from music import STM_BGM  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SETTING_CPP = ROOT / "vendor/Petari/src/Game/AudioLib/AudBgmSetting.cpp"
RHYTHM_KEYS = {0x18: 0, 0x1E: 1, 0x1B: 2, 0x19: 3, 0x1A: 4}  # AudRhythmSeqParser mRhythmType0-4
RATE = 32000


def multi_bgms():
    """cMultiBgmSet: [(BGM sequence id name, STM id name)]"""
    src = SETTING_CPP.read_text()
    body = src[src.index("cMultiBgmSet[] = {"):]
    return re.findall(r"\{BGM_(\w+), STM_(\w+),", body[:body.index("};")])


def is_rhythm_track(t):
    """the tracks AudRhythmMeSystem::setSeq gives the rhythm parser: the root and track 0's track 0"""
    if t.parent is None:
        return True
    p = t.parent
    return p.parent is not None and p.parent.parent is None and p.parent.children.get(0) is p and p.children.get(0) is t


def timeline(seq, baa, seconds):
    """run the sequence `seconds` long (None: as bgmseq renders it, to its loop's end or its end) -> [(sample, kind,
    value)]"""
    ev, clock = [], [0.0]
    orig = S.Track.note_on, S.Track.exec, S.Track.start, S.Track.tick

    def note_on(self, nid, key, vel, chans):  # AudRhythmSeqParser::execNoteOnMidi
        if is_rhythm_track(self):
            at = round(clock[0] * RATE)
            typ = RHYTHM_KEYS.get(key, -1)  # the raw pitch (parseNoteOn)
            if typ == 0 and vel - 10 < 100:
                ev.append((at, 0, 5 + 1))
            ev.append((at, 0, typ + 1))
        orig[0](self, nid, key, vel, chans)

    def exec_(self, op, a, chans):  # AudRhythmSeqParser::parse: 0xE2 scale, 0xE3 chord
        if op in (0xE2, 0xE3) and is_rhythm_track(self):
            ev.append((round(clock[0] * RATE), 2 if op == 0xE2 else 1, a[0] & 0xFF))
        orig[1](self, op, a, chans)

    def tick(self, chans):  # only the rhythm parser's tracks are run: the others never reach the callbacks
        kids = self.children
        self.children = {k: c for k, c in kids.items() if k == 0 and (self.parent is None or self.parent.parent is None)}
        orig[3](self, chans)
        self.children = {**kids, **self.children}

    S.Track.note_on, S.Track.exec, S.Track.start, S.Track.tick = note_on, exec_, lambda *_: None, tick
    try:
        root = Q.LoopTrack(seq, baa, 0)
        chans, acc = [], 1.0
        limit = int((seconds or Q.MAX_SECONDS) * S.DAC / S.SUB)
        for n in range(limit):
            root.now = n
            clock[0] = n * S.SUB / S.DAC
            while acc >= 1.0:
                acc -= 1.0
                root.tick(chans)
            acc += root.interval()
            if seconds is None and root.loop and n >= root.loop[1]:
                break
            t0 = root.children.get(0)
            if root.done and (not t0 or t0.done and (not t0.children.get(0) or t0.children[0].done)):
                break
    finally:
        S.Track.note_on, S.Track.exec, S.Track.start, S.Track.tick = orig
    return ev


def write(path, chord_id, cit, ev):
    head = 24
    out = struct.pack("<4s5I", b"SMGR", chord_id, head, len(cit), head + len(cit) + (-len(cit) % 4), len(ev))
    out += cit + bytes(-len(cit) % 4)
    assert all(at >> 1 < 1 << 22 and v < 256 for at, _, v in ev)
    out += b"".join(struct.pack("<I", at >> 1 << 10 | k << 8 | v) for at, k, v in ev)
    Path(path).write_bytes(out)


def main(fs, out):
    fs, out = Path(fs), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    baa, seqs = S.Baa(fs), Q.jaiseq(fs)
    chords = [b for _, b in rarc_files((fs / "AudioRes/Info/JaiChord.arc").read_bytes())]  # resource id = file order
    jobs = []  # (stem, BGM name, seconds or None)
    for bgm, stm in multi_bgms():
        stem = STM_BGM.get(stm)
        ast = fs / "AudioRes/Stream" / f"{stem}.ast"
        if stem and ast.exists():
            samples = struct.unpack_from(">I", ast.read_bytes()[:0x20], 0x14)[0]
            jobs.append((stem, "BGM_" + bgm, samples / RATE))
    jobs += [("SEQ_" + n[4:].lower(), n, None) for n in Q.SEQ_BGMS]
    for stem, bgm, seconds in jobs:
        _, idx, chord, sw = Q.bgm_item(baa, bgm)
        if not sw & 1 or chord == 0xFFFF:  # AudSoundInfo::isRhythmBgm / getChordResIdBgm: no ME over it
            continue
        ev = timeline(seqs[idx], baa, seconds)
        write(out / f"{stem}.rhy", chord, chords[chord], ev)
        beats = [at for at, k, v in ev if k == 0 and v == 1]
        gap = (beats[1] - beats[0]) / RATE if len(beats) > 1 else 0
        print(f"{stem}.rhy: {bgm} chord table {chord}, {len(ev)} events, {len(beats)} beats ({gap:.3f} s apart), "
              f"{sum(k == 1 for _, k, _ in ev)} chords")


def selftest(fs=ROOT / "data/fs/files"):
    fs = Path(fs)
    baa, seqs = S.Baa(fs), Q.jaiseq(fs)
    assert ("GALAXY_01", "GALAXY_01") in multi_bgms() and len(multi_bgms()) > 60
    _, idx, chord, sw = Q.bgm_item(baa, "BGM_GALAXY_01")
    assert sw & 1 and chord == 40
    ev = timeline(seqs[idx], baa, 20.0)
    beats = [at for at, k, v in ev if k == 0 and v == 1]
    assert len(beats) > 10 and sum(k == 1 for _, k, _ in ev) > 0, ev[:20]
    gaps = {round((b - a) / RATE, 2) for a, b in zip(beats, beats[1:])}
    assert len(gaps) <= 3, gaps  # a steady beat
    assert all(a[0] <= b[0] for a, b in zip(ev, ev[1:]))
    print("rhythm selftest ok", len(ev), "events", gaps)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        selftest(*sys.argv[2:])
    else:
        main(sys.argv[1], sys.argv[2])
