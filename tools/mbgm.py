"""Multi BGMs (Petari AudMultiBgm, AudBgmSetting, AudBgmTempoAdjuster): an MBGM_ id plays a stream (STM_: one or two
stereo pairs, volumes A / B) and its sequence (BGM_: the rhythm handle) together, the sequence kept on the stream's
beats (beat k at k x 60 x 32000 / tempo x beatMul stream samples: cMultiBgmSet's intro / loop beats over its loop
samples). The sequence's tracks follow the BGM's mute state (cMuteState through cBgmSettingInfo: per track nibble
0 fade out, 1 fade in, 2 mute, 3 unmute, bit 3 auto mute; volumes A / B / 254, 0xFF unchanged); without a table every
track plays (AudSingleBgm / AudMultiBgm::changeTrackMuteState). prepare applies state 0.

Each MBGM is mixed offline, per state of its table, into the music streams' format (tools/music.py .pcm: 'SMGM', rate
32000, 2 channels, loop start byte), out/music/<stream>.pcm for state 0 and <stream>@<k>.pcm for state k (game.c
bgm_state switches between them at the same place). Items' volumes (BST: the stream's and the sequence's u8 / 255) set
the balance; one gain for all of them keeps the loudest under 0.9 (the same as tools/bgmseq.py's).

The sequence is rendered twice over its loop (tools/sound.py, tools/bgmseq.py render), resampled onto the stream's
time line piecewise (its intro onto the stream's intro, a loop onto the stream's loop: the tempo adjuster's job), and
its second loop is the one mixed in (the notes held over the loop's end ring into its start).

ponytail: the adjuster's 500-sample lead (calcAdjustRate) is not kept (the sequence on the beat itself); a track's
nibble 3 (unmute) keeps state 0's level; the BGM_GALAXY_01 x3 fade time and the extra chords are not used.

usage: python tools/mbgm.py data/fs/files out/music [MBGM_NAME...]
       python tools/mbgm.py --selftest
"""
import re
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bgmseq as Q  # noqa: E402
import sound as S  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PETARI = ROOT / "vendor/Petari"


def sound_ids():
    """AudSoundId.hpp: name -> value"""
    src = (PETARI / "include/Game/AudioLib/AudSoundId.hpp").read_text(encoding="utf-8")
    return {m.group(1): int(m.group(2), 0) for m in re.finditer(r"\b([A-Z][A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", src)}


def setting_tables():
    """AudBgmSetting.cpp -> (cMultiBgmSet rows, cBgmSettingInfo mute indices, cMuteState tables)"""
    src = (PETARI / "src/Game/AudioLib/AudBgmSetting.cpp").read_text(encoding="utf-8")
    sets = [(m.group(1), m.group(2), float(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7)))
            for m in re.finditer(r"\{(BGM_\w+), (STM_\w+), ([\d.]+)f, (\d+), (\d+), (\d+), (\d+)\}", src)]
    info = src[src.index("cBgmSettingInfo[] = {"):]
    info = info[:info.index("};")]
    mute_idx = [int(a) for a, _ in re.findall(r"\{\s*(-?\d+),\s*(-?\d+)\}", info)]
    ms = src[src.index("cMuteState[][8] = {"):]
    ms = ms[:ms.index("\n    };")]
    rows = [[int(x, 16) for x in r] for r in re.findall(r"\{((?:0x[0-9A-Fa-f]{2},\s*){9}0x[0-9A-Fa-f]{2})\}", ms)
            for r in [re.findall(r"0x[0-9A-Fa-f]{2}", r)]]
    tables = [rows[i:i + 8] for i in range(0, len(rows), 8)]
    return sets, mute_idx, tables


def stream_files(baa):
    """the stream file table (STM index -> AudioRes/Stream file stem), in the BAA's string order"""
    return [m.group(1).decode() for m in re.finditer(rb"/AudioRes/Stream/([A-Za-z0-9_]+)\.ast", baa.d)]


def item_volume(baa, name):
    return Q.bgm_item(baa, name)[0]


def read_ast(path):
    d = Path(path).read_bytes()
    _, _, fmt, bits, chans, _, rate, samples, ls, le = struct.unpack_from(">4sIHHHHIIII", d, 0)
    out, o = [], 0x40
    d = d + bytes(0x10000)
    while o + 0x20 <= len(d) and d[o:o + 4] == b"BLCK":
        n = struct.unpack_from(">I", d, o + 4)[0]
        o += 0x20
        out.append(np.stack([np.frombuffer(d, ">i2", n // 2, o + c * n) for c in range(chans)], axis=1))
        o += n * chans
    pcm = np.concatenate(out)[:samples].astype(np.float32) / 32768.0
    return rate, ls, le, pcm


def track_no(t):
    """getChildTrack(i): the root's child i >> 4, its child i & 15 (-1 above that level)"""
    while t is not None and t.parent is not None and t.parent.parent is not None and t.parent.parent.parent is not None:
        t = t.parent
    if t is None or t.parent is None or t.parent.parent is None:
        return -1
    pk = next(k for k, c in t.parent.parent.children.items() if c is t.parent)
    ck = next(k for k, c in t.parent.children.items() if c is t)
    return pk * 16 + ck


def render_tracks(baa, seq, vol, seconds, gains):
    """bgmseq.render with a gain per track number (gains: list of 16, missing tracks 1)"""
    root = Q.LoopTrack(seq, baa, 0, se_vol=vol)
    chans, out, acc = [], [], 1.0
    for n in range(int(seconds * S.DAC / S.SUB)):
        root.now = n
        while acc >= 1.0:
            acc -= 1.0
            root.tick(chans)
        acc += root.interval()
        buf = np.zeros((S.SUB, 2), np.float32)
        tmp = np.zeros(S.SUB, np.float32)
        for c in chans:
            if c.alive:
                tmp[:] = 0
                c.render(tmp)
                k = track_no(c.track)
                g = gains[k] if 0 <= k < len(gains) else 1.0
                if g == 0.0:
                    continue
                p = Q.pan_of(c.track)
                buf[:, 0] += g * tmp * np.sin((1.0 - p) * np.pi / 2)
                buf[:, 1] += g * tmp * np.sin(p * np.pi / 2)
        chans = [c for c in chans if c.alive]
        out.append(buf)
    return np.concatenate(out), root.loop


def state_levels(table, state):
    """-> (16 track gains, volume A, volume B) of a mute state (None table: every track on, A = B = 1)"""
    if table is None:
        return [1.0] * 16, 1.0, 1.0
    row, base = table[state], table[0]
    gains = []
    for i in range(16):
        nib = lambda r: (r[i // 2] >> 4 if i % 2 == 0 else r[i // 2]) & 7
        s = nib(row)
        gains.append(1.0 if s == 1 else 0.0 if s in (0, 2) else (1.0 if nib(base) == 1 else 0.0))
    va = row[8] / 254.0 if row[8] != 0xFF else 1.0
    vb = row[9] / 254.0 if row[9] != 0xFF else 1.0
    return gains, va, vb


def to_rate(pcm, src, dst, n):
    x = np.linspace(0, len(pcm) - 1, n) if src == dst else np.arange(n) * (src / dst)
    x = np.minimum(x, len(pcm) - 1)
    return np.stack([np.interp(x, np.arange(len(pcm)), pcm[:, c]) for c in (0, 1)], 1).astype(np.float32)


def warp(seq, seq_rate, seq_ls, seq_le, ls, le, n):
    """the sequence (seq_rate, its loop seq_ls .. seq_le in its samples) onto the stream's time line (32000, loop
    ls .. le), n samples: intro onto intro, each loop onto the stream's loop"""
    t = np.arange(n, dtype=np.float64)
    src = np.where(t < ls, t * seq_ls / max(ls, 1), seq_ls + (t - ls) * (seq_le - seq_ls) / max(le - ls, 1))
    src = np.minimum(src, len(seq) - 1)
    return np.stack([np.interp(src, np.arange(len(seq)), seq[:, c]) for c in (0, 1)], 1).astype(np.float32)


def mix(baa, seqs, names, row, mute_idx, tables):
    """-> (stream stem, [(state, stereo float32 at 32000, loop start sample)])"""
    seq_name, stm_name, beat_mul, intro, loop_beats, ls_t, le_t = row
    ids = sound_ids()
    stem = names[ids[stm_name] & 0xFFFF]
    rate, ls, le, st = read_ast(ROOT / "data/fs/files/AudioRes/Stream" / f"{stem}.ast")
    assert rate == 32000, rate
    if (ls, le) != (ls_t, le_t):
        print(f"  {stem}: AST loop {ls} .. {le}, table {ls_t} .. {le_t}")
    a, b = st[:, 0:2], (st[:, 2:4] if st.shape[1] >= 4 else None)
    svol, qvol = item_volume(baa, stm_name), item_volume(baa, seq_name)
    _, qidx, _, _ = Q.bgm_item(baa, seq_name)
    widx = ids[seq_name] & 0xFFFF
    table = tables[mute_idx[widx]] if widx < len(mute_idx) and mute_idx[widx] >= 0 else None
    states = range(8) if table is not None else [0]
    out, done = [], {}
    for k in states:
        gains, va, vb = state_levels(table, k)
        key = (tuple(gains), va, vb)
        if key in done:  # a state the same as an earlier one: its file is that one's
            out.append((k, None, ls, done[key][1]))
            continue
        seconds = (le + (le - ls)) / 32000.0 + 2.0
        q, loop = render_tracks(baa, seqs[qidx], qvol, seconds, gains)
        if loop is None:
            raise ValueError(f"{seq_name}: no loop in its sequence")
        qls, qle = loop[0] * S.SUB, loop[1] * S.SUB
        # the stream's beat time line: beat intro at ls (table), so the sequence's loop start goes there
        body = warp(q, S.DAC, qls, qle, ls, le, le + (le - ls))
        seq_mix = np.concatenate([body[:ls], body[le:le + (le - ls)]])  # the intro, then the second loop
        m = a[:le] * va * svol + (b[:le] * vb * svol if b is not None else 0.0) + seq_mix[:le] * 1.0
        done[key] = (m, k)
        out.append((k, m, ls, k))
        print(f"  {stem} state {k}: seq loop {qls / S.DAC:.2f} .. {qle / S.DAC:.2f} s, stream {ls / 32000:.2f} .. "
              f"{le / 32000:.2f} s, A {va:.2f} B {vb:.2f}, tracks on {sum(1 for g in gains if g)}")
    return stem, out


def main(fs, outdir, only):
    baa = S.Baa(fs)
    seqs, names = Q.jaiseq(fs), stream_files(S.Baa(fs))
    sets, mute_idx, tables = setting_tables()
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    ids = sound_ids()
    mbgm = {n: v & 0xFFFF for n, v in ids.items() if n.startswith("MBGM_")}
    want = [n for n in sorted(mbgm, key=mbgm.get) if not only or n in only]
    mixes = []
    for n in want:
        i = mbgm[n]
        if i >= len(sets):
            continue
        print(n, sets[i][:2])
        mixes.append(mix(baa, seqs, names, sets[i], mute_idx, tables))
    peak = max(float(np.abs(m).max()) for _, ms in mixes for _, m, _, _ in ms if m is not None)
    gain = 0.9 / max(peak, 1e-6)
    rows = []
    for stem, ms in mixes:
        smap = [0] * 8
        for k, m, ls, src in ms:
            smap[k] = src
            if m is None:
                continue
            data = (np.clip(m * gain, -1, 1) * 32767).astype("<i2").tobytes()
            path = out / (f"{stem}.pcm" if k == 0 else f"{stem}@{k}.pcm")
            path.write_bytes(struct.pack("<4sIII", b"SMGM", 32000, 2, ls * 4) + data)
        if any(smap):
            rows.append(f'    {{"{stem}", {{{", ".join(map(str, smap))}}}}},')
    if not only:
        (ROOT / "engine/src/mbgm_tab.h").write_text("\n".join([
            "/* written by tools/mbgm.py: the multi BGMs with mute states, the file each state plays (<stream>@<k>.pcm,",
            " * 0: <stream>.pcm) for MR::setStageBGMState */",
            "static const struct { const char *track; unsigned char file[8]; } mbgm_tab[] = {", *rows, "};", ""]))
    print(f"{len(mixes)} multi BGMs, gain {gain:.3f}")


def selftest():
    sets, mute_idx, tables = setting_tables()
    assert sets[0][:2] == ("BGM_LIBRARY", "STM_LIBRARY") and len(tables[0]) == 8 and len(tables[0][0]) == 10
    g, va, vb = state_levels(tables[1], 3)  # {0x21, 0x10, ...}: tracks 0 mute, 1 in, 2 in, 3 out
    assert g[:4] == [0.0, 1.0, 1.0, 0.0] and va == 0xFE / 254.0, (g[:4], va)
    assert state_levels(None, 0)[0] == [1.0] * 16
    baa = S.Baa(ROOT / "data/fs/files")
    names = stream_files(baa)
    assert names[sound_ids()["STM_GALAXY_24"] & 0xFFFF] == "SMG_galaxy24_multi", names[:3]
    print("mbgm selftest ok")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        selftest()
    else:
        main(sys.argv[1], sys.argv[2], set(sys.argv[3:]))
