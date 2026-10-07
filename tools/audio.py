"""Wave data each galaxy/scenario keeps in audio RAM, Wii (.aw bytes) vs PS2 (SPU ADPCM).

Banks: BAA command `'ws  ' {bank no, WSYS offset, ?}` (JAUAudioArcInterpreter.cpp:31-36) -> WSYS (JASWSParser.hpp:
archive bank offset @+0x10 -> 'WINF'
{count, offsets[]} -> archive {name[0x70], waveCount, wave offsets} -> wave {format @1, AW length @0xC,
sampleCount @0x1C}). loadWaveArc(bank) loads every archive of the bank (JAUSectionHeap.cpp:341-356).
Which banks: AudSceneMgr.cpp tables (static, player, per-stage SE/BGM sets, per-scenario SE sets).
PS2: every wave re-encoded to SPU ADPCM, 16 bytes per 28 samples (refs/psx-spx-spu.md:151-159).

usage: python tools/audio.py data/fs/files        |  python tools/audio.py --selftest
"""
import re
import struct
import sys
from pathlib import Path

from budget import rarc_files, yaz0, u32

SCENE_CPP = Path(__file__).resolve().parent.parent / "vendor/Petari/src/Game/AudioLib/AudSceneMgr.cpp"
STATIC = [0, 1, 2, 3, 4, 5, 6, 8, 11]  # loadStaticResource
SYSTEM = [7]                           # isLoadDoneSystemInit waits for bank 7
PLAYER = (34, 1)                       # Mario: loadWaveArc(34, 2) -> archive bit 1


def banks(smr):
    """-> {bank id: [(file name, wii bytes from AW lengths, ps2 bytes)]} per archive."""
    out = {}
    cmds = []  # ('ws  ', bank no, offset) whose offset really holds a WSYS
    c = smr.find(b"ws  ")
    while 0 <= c <= len(smr) - 16:
        bank, off = u32(smr, c + 4), u32(smr, c + 8)
        if c % 4 == 0 and smr[off:off + 4] == b"WSYS":
            cmds.append((bank, off))
        c = smr.find(b"ws  ", c + 1)
    for bid, o in cmds:
        winf = o + u32(smr, o + 0x10)
        if smr[winf:winf + 4] != b"WINF":
            continue
        arcs = []
        for i in range(u32(smr, winf + 4)):
            a = o + u32(smr, winf + 8 + 4 * i)
            name = smr[a:a + 0x70].split(b"\0")[0].decode("ascii", "replace")
            wii = ps2 = 0
            for j in range(u32(smr, a + 0x70)):
                w = o + u32(smr, a + 0x74 + 4 * j)
                wii += u32(smr, w + 0x0C)
                ps2 += -(-u32(smr, w + 0x1C) // 28) * 16
            arcs.append((name, wii, ps2))
        out[bid] = arcs
    return out


def cmds(smr, tag, magic):
    """BAA commands `tag {number, offset, ...}` whose offset holds `magic` -> [(number, offset)]."""
    out, c = [], smr.find(tag)
    while 0 <= c <= len(smr) - 12:
        n, off = u32(smr, c + 4), u32(smr, c + 8)
        if c % 4 == 0 and smr[off:off + 4] == magic:
            out.append((n, off))
        c = smr.find(tag, c + 1)
    return out


def waves(smr):
    """-> {wave bank: [(archive index, wave id, wii bytes, ps2 bytes)]}. Wave id from the ctrl table
    (JASWSParser.hpp TCtrlGroup @+0x14 -> scene offsets @+0xC -> scene ctrl @+0xC -> {count @4, offsets @8},
    id = low half of the TCtrlWave word, JASWSParser.cpp createBasicWaveBank)."""
    out = {}
    for bid, o in cmds(smr, b"ws  ", b"WSYS"):
        winf, grp = o + u32(smr, o + 0x10), o + u32(smr, o + 0x14)
        rows = []
        for i in range(u32(smr, winf + 4)):
            a = o + u32(smr, winf + 8 + 4 * i)
            ctrl = o + u32(smr, o + u32(smr, grp + 0xC + 4 * i) + 0xC)
            for j in range(u32(smr, a + 0x70)):
                w = o + u32(smr, a + 0x74 + 4 * j)
                wid = u32(smr, o + u32(smr, ctrl + 8 + 4 * j)) & 0xFFFF
                rows.append((i, wid, u32(smr, w + 0x0C), -(-u32(smr, w + 0x1C) // 28) * 16))
        out[bid] = rows
    return out


def instruments(smr):
    """-> {IBNK bank number: (wave bank, {program: [(high key, set of wave ids)]})}; a percussion program has one
    entry per key (high key = that key). Bank number = IBNK word 2
    (JASBNKParser.hpp:181-184), what BMS bank commands name; wave bank = first word of its `bnk ` command
    (JAUSectionHeap.cpp:394-410). IBNK v1 'LIST' -> 'Inst'/'Perc' (JASBNKParser.cpp:93-195): program = LIST index,
    velocity region = 4 words, wave id = low half of word 1."""
    out = {}
    for wb, o in cmds(smr, b"bnk ", b"IBNK"):
        progs = {}
        out[u32(smr, o + 8)] = (wb, progs)
        end, ch = o + u32(smr, o + 4), o + 0x20
        lst = None
        while ch < end:  # findChunk: next = (chunk + 0xB + size) & ~3
            if smr[ch:ch + 4] == b"LIST":
                lst = ch
                break
            ch = (ch + 0xB + u32(smr, ch + 4)) & ~3
        if lst is None:
            continue
        for i in range(u32(smr, lst + 8)):
            off = u32(smr, lst + 0xC + 4 * i)
            if not off:
                continue
            regions = progs.setdefault(i, [])
            d = o + off
            kind, d = smr[d:d + 4], d + 4
            if kind == b"Inst":
                d += 4 + 4 * u32(smr, d)            # oscillators
                d += 4 + 4 * u32(smr, d)            # effects
                keys, d = u32(smr, d), d + 4
                for _ in range(keys):
                    high, velos, d = u32(smr, d) >> 24, u32(smr, d + 4), d + 8   # setHighKey(data[0] >> 24)
                    ids = set()
                    for _ in range(velos):
                        ids.add(u32(smr, d + 4) & 0xFFFF); d += 16
                    regions.append((high, ids))
            elif kind == b"Perc":
                count, d = u32(smr, d), d + 4
                for j in range(count):          # drump->setPerc(j, ...): percussion j answers key j
                    po = u32(smr, d + 4 * j)
                    if not po:
                        continue
                    q = o + po + 16                 # 'Pmap' + TPercData, then effects
                    q += 4 + 4 * u32(smr, q)
                    velos, q = u32(smr, q), q + 4
                    ids = set()
                    for _ in range(velos):
                        ids.add(u32(smr, q + 4) & 0xFFFF); q += 16
                    regions.append((-j - 1, ids))   # negative = percussion key j
    return out


def referenced(smr):
    """-> {wave bank: set of wave ids named by any instrument or percussion of the IBNKs bound to it}."""
    out = {}
    for wb, progs in instruments(smr).values():
        out.setdefault(wb, set()).update(*(ids for regs in progs.values() for _, ids in regs))
    return out


DYN_KEY = "all"   # a key read from a register: "all" regions (what the game may pick) or "first" (one variant kept, D)


def key_waves(regions, key):
    """Wave ids a note `key` plays on a program: percussion entry `key`, else the first key region whose high key
    is >= key (JASBasicInst key regions are ordered by high key). A key read from a register ("dyn") plays any."""
    if not isinstance(key, int):
        if not regions:
            return set()
        return set(regions[0][1]) if DYN_KEY == "first" else set().union(*(ids for _, ids in regions))
    perc = [ids for h, ids in regions if h == -key - 1]
    if any(h < 0 for h, _ in regions):
        return perc[0] if perc else set()
    return next((ids for h, ids in regions if key <= h), set())


def sequences(fs, smr):
    """-> (BGM sequences, SE sequences) as (data, start offset). BGM: AudioRes/Seqs/JaiSeq.arc (Yaz0 BMS files,
    AudSystem.cpp:44,114-115). SE: BAA `bsc  {start, end}` (JAUAudioArcInterpreter.cpp:53-58) -> 'SC' collection
    {u16 groups @2, offsets @8} -> table {count, sequence offsets} (JAUSeqCollection.hpp:8-18)."""
    bgm = [(yaz0(d), 0) for n, d in rarc_files((Path(fs) / "AudioRes/Seqs/JaiSeq.arc").read_bytes()) if n.endswith(".szs")]
    se, c = [], smr.find(b"bsc ")
    while c >= 0:
        s0, s1 = u32(smr, c + 4), u32(smr, c + 8)
        if c % 4 == 0 and smr[s0:s0 + 2] == b"SC":
            coll = smr[s0:s1]
            for g in range(int.from_bytes(coll[2:4], "big")):
                t = u32(coll, 8 + 4 * g)
                se += [(coll, u32(coll, t + 4 + 4 * k)) for k in range(u32(coll, t))]
        c = smr.find(b"bsc ", c + 1)
    return bgm, se


def se_names(smr):
    """BSTN section 0 (sound effects) -> [(category, [sound names])] per group. Layout JAUSoundTable.hpp:171-209:
    root @word 3 {sections, offsets}; section {groups, global, group offsets}; group {items, category name, item
    name offsets}; all offsets from the table start. Group g / item k = SE sequence collection group g / entry k
    (JAUSeqCollection.cpp:65-69: sound id group / wave id)."""
    c = smr.find(b"bstn")
    while c >= 0:
        s0, s1 = u32(smr, c + 4), u32(smr, c + 8)
        if c % 4 == 0 and smr[s0:s0 + 4] == b"BSTN":
            d = smr[s0:s1]
            cstr = lambda o: d[o:d.index(b"\0", o)].decode("ascii", "replace")
            sec = u32(d, u32(d, 0xC) + 4)
            out = []
            for g in range(u32(d, sec)):
                go = u32(d, sec + 8 + 4 * g)
                out.append((cstr(u32(d, go + 4)), [cstr(u32(d, go + 8 + 4 * k)) for k in range(u32(d, go))]) if go else ("", []))
            return out
        c = smr.find(b"bstn", c + 1)
    return []


def reached(seqs, inst):
    """Waves (wave bank, wave id) played by notes of these sequences, via bms.analyse and the IBNK programs.
    -> (waves, notes with no bank set, bad paths). Notes on banks with no IBNK resolve nothing."""
    import bms
    out, unset, bad = set(), 0, 0
    for data, start in seqs:
        r = bms.analyse(data, start)
        bad += r["bad"]
        for (bk, pg), n in r["pairs"].items():
            if bk is None or bk == bms.DYN or pg == bms.DYN:
                unset += n
            elif bk in inst:
                wb, progs = inst[bk]
                for k in r["keys"][(bk, pg)]:
                    out |= {(wb, w) for w in key_waves(progs.get(pg, []), k)}
    return out, unset, bad


def tables():
    src = SCENE_CPP.read_text(encoding="utf-8")
    def tbl(name):
        body = src[src.index(name):]
        body = body[body.index("{") + 1: body.index("};")]
        return [[int(x) for x in re.findall(r"-?\d+", row)] for row in re.findall(r"\{([-\d,\s]+)\}", body)]
    scenes = re.findall(r'\{"Game", "(\w+)", (-?\d+), (-?\d+)\}', src)
    return ({n: (int(s), int(c)) for n, s, c in scenes}, tbl("cScenarioNoToWaveSet"), tbl("cWaveSetInfo"),
            tbl("cBgmWaveSet"), tbl("cSeWaveSet"), tbl("cSeScenarioWaveSet"))


def scene_sets():
    """-> {(stage, scenario or 0): set of (bank, archive index or None=all)}."""
    scenes, scen2set, info, bgm, se, se_scen = tables()
    base = {(b, None) for b in STATIC + SYSTEM} | {PLAYER}
    out = {}
    for stage, (sid, cid) in scenes.items():
        bgm_id, se_id = info[sid]
        s = base | {(b, None) for b in bgm[bgm_id] + se[se_id] if b >= 0}
        out[(stage, 0)] = s
        if cid >= 0:
            for no, ws in enumerate(scen2set[cid], 1):
                if ws >= 0:
                    out[(stage, no)] = s | {(b, None) for b in se_scen[ws] if b >= 0}
    return out


def size(sel, bk, k):
    return sum(a[k] for b, idx in sel for i, a in enumerate(bk.get(b, [])) if idx is None or i == idx)


def main(fs):
    fs = Path(fs)
    smr = yaz0((fs / "AudioRes/SMR.szs").read_bytes())
    bk = banks(smr)
    waves = fs / "AudioRes/Waves"
    disk = {p.name.lower(): p.stat().st_size for p in waves.glob("*.aw")}
    named = {a[0].lower() for arcs in bk.values() for a in arcs}
    M = 2**20
    print(f"banks {len(bk)}, archives {len(named)}, on disc {len(disk)}, missing {sorted(named - set(disk))[:5]}")
    rows = sorted(((size(s, bk, 2), size(s, bk, 1), k) for k, s in scene_sets().items()), reverse=True)
    base = {(b, None) for b in STATIC + SYSTEM} | {PLAYER}
    print(f"always loaded: wii {size(base, bk, 1) / M:.2f} MB  ps2 {size(base, bk, 2) / M:.2f} MB")
    print(f"{'stage':28} sc  {'wii MB':>7} {'ps2 MB':>7}")
    for ps2, wii, (st, no) in rows[:15]:
        print(f"{st:28} {no:2}  {wii / M:7.2f} {ps2 / M:7.2f}")
    return rows


def selftest():
    # one WSYS, id 9, one archive "a.aw" with one ADPCM wave: AW length 32, 56 samples -> ps2 32
    w = bytearray(0x200)
    w[0:4] = b"WSYS"; struct.pack_into(">II", w, 8, 123, 0)  # WSYS's own id is not the bank number
    struct.pack_into(">I", w, 0x10, 0x20)
    w[0x20:0x24] = b"WINF"; struct.pack_into(">II", w, 0x24, 1, 0x40)
    w[0x40:0x44] = b"a.aw"; struct.pack_into(">II", w, 0x40 + 0x70, 1, 0x100)
    struct.pack_into(">I", w, 0x100 + 0x0C, 32); struct.pack_into(">I", w, 0x100 + 0x1C, 56)
    baa = b"AA_<" + b"ws  " + struct.pack(">III", 9, 0x20, 0) + b">_AA" + bytes(8)
    blob = baa + bytes(w)
    blob = blob[:4] + b"ws  " + struct.pack(">III", 9, len(baa), 0) + blob[20:]
    assert banks(blob) == {9: [("a.aw", 32, 32)]}, banks(blob)
    assert size({(9, None)}, {9: [("a", 1, 2), ("b", 10, 20)]}, 2) == 22
    assert size({(9, 1)}, {9: [("a", 1, 2), ("b", 10, 20)]}, 2) == 20
    s = scene_sets()
    assert (34, 1) in s[("OceanPhantomCaveGalaxy", 0)] and (18, None) in s[("OceanPhantomCaveGalaxy", 1)]
    # IBNK v1 bound to wave bank 5: LIST with one 'Inst' (no osc/effects, 1 key, 2 velos -> waves 7, 8)
    # and one 'Perc' (1 perc, 1 velo -> wave 9)
    ib = bytearray(0x100)
    ib[0:4] = b"IBNK"; struct.pack_into(">II", ib, 4, 0x100, 0); struct.pack_into(">I", ib, 0xC, 1)
    ib[0x20:0x24] = b"LIST"; struct.pack_into(">III", ib, 0x24, 8, 2, 0x40); struct.pack_into(">I", ib, 0x30, 0x80)
    ib[0x40:0x44] = b"Inst"; struct.pack_into(">IIIII", ib, 0x44, 0, 0, 1, 0x7F << 24, 2)
    struct.pack_into(">4I", ib, 0x58, 0, 7, 0, 0); struct.pack_into(">4I", ib, 0x68, 0, 8, 0, 0)
    ib[0x80:0x84] = b"Perc"; struct.pack_into(">II", ib, 0x84, 1, 0xA0)
    ib[0xA0:0xA4] = b"Pmap"; struct.pack_into(">II", ib, 0xB0, 0, 1); struct.pack_into(">4I", ib, 0xB8, 0, 9, 0, 0)
    baa = b"AA_<" + b"bnk " + struct.pack(">II", 5, 20) + bytes(4) + bytes(ib)
    assert referenced(baa) == {5: {7, 8, 9}}, referenced(baa)
    regs = instruments(baa)[0][1]
    assert key_waves(regs[0], 60) == {7, 8} and key_waves(regs[0], 0x7F) == {7, 8} and key_waves(regs[0], 200) == set()
    assert key_waves(regs[1], 0) == {9} and key_waves(regs[1], 1) == set() and key_waves(regs[1], "dyn") == {9}
    print("selftest ok")


if __name__ == "__main__":
    selftest() if sys.argv[1:] == ["--selftest"] else main(sys.argv[1])
