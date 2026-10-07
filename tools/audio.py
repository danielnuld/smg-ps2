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

from budget import yaz0, u32

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
    print("selftest ok")


if __name__ == "__main__":
    selftest() if sys.argv[1:] == ["--selftest"] else main(sys.argv[1])
