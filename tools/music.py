"""Music streams (milestone 6, music through PCM streams as phase 0c decided): AudioRes/Stream/*.ast -> PS2 PCM.

usage: python tools/music.py data/fs/files out/music [names...]   (no names: every stream the table uses)
       python tools/music.py --table data/fs/files engine/src/music_tab.h
       python tools/music.py --selftest

AST (Wii stream): header 'STRM' {u32 data size, u16 format (1 = PCM16), u16 bits, u16 channels, u16 ?, u32 rate,
u32 samples, u32 loop start, u32 loop end, u32 first block size} then blocks 'BLCK' {u32 size per channel, 0x18 pad}
followed by each channel's `size` bytes of big-endian PCM16. Output <name>.pcm: u32 'SMGM', u32 rate, u32 channels,
u32 loop start byte offset (into the samples), then interleaved little-endian PCM16 (what audsrv plays). Channels
beyond the first two (the *_multi tracks layer instruments) are dropped. ponytail: no resampling (32 kHz stays).
"""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from budget import bcsv_column, rarc_files  # noqa: E402
from bgmseq import SEQ_BGMS  # noqa: E402

# Stage -> BGM (Petari AudStageBgmWrap.cpp sMAStageBgmData / sMASenarioBgmData, AudStageBgmTable.cpp entry 0 for
# stages missing there); sound ids by name, a scenario group's -1 falls back to the stage's own id.
STAGE_BGM = {
    "HellProminenceGalaxy": "GALAXY_02", "OceanRingGalaxy": "GALAXY_03", "OceanFloaterLandGalaxy": "GALAXY_04",
    "EggStarGalaxy": "GALAXY_01", "PhantomGalaxy": "GALAXY_INTER", "AstroDome": ("ASTRO_DOME", 4),
    "HeavenlyBeachGalaxy": "GALAXY_03", "AstroGalaxy": ("ASTRO_OUT", 1), "CosmosGardenGalaxy": "GALAXY_14",
    "HoneyBeeKingdomGalaxy": ("GALAXY_10", 0), "BeltConveyerExGalaxy": "GALAXY_17", "KoopaBattleVs1Galaxy": "GALAXY_15",
    "SurfingLv1Galaxy": "GALAXY_03", "SurfingLv2Galaxy": "GALAXY_03", "CubeBubbleExLv1Galaxy": "GALAXY_12",
    "CubeBubbleExLv2Galaxy": "GALAXY_12", "FlipPanelExGalaxy": "GALAXY_13", "TamakoroExLv1Galaxy": "GALAXY_17",
    "TamakoroExLv2Galaxy": "GALAXY_17", "BreakDownPlanetGalaxy": "GALAXY_13", "SandClockGalaxy": "GALAXY_19",
    "KoopaBattleVs2Galaxy": "GALAXY_15", "KoopaJrShipLv1Galaxy": "GALAXY_09", "HoneyBeeExGalaxy": "GALAXY_17",
    "TriLegLv1Galaxy": "GALAXY_01_TOMB", "TriLegLv2Galaxy": "GALAXY_01_TOMB", "CocoonExGalaxy": "GALAXY_12",
    "ReverseKingdomGalaxy": "GALAXY_10", "DarkRoomGalaxy": "GALAXY_12", "FactoryGalaxy": "GALAXY_22",
    "SkullSharkGalaxy": "GALAXY_23", "KoopaJrDriverGalaxy": "GALAXY_01_TOMB", "FloaterOtaKingGalaxy": "GALAXY_01_TOMB",
    "TransformationExGalaxy": "GALAXY_13", "LibraryRoom": "LIBRARY", "TearDropGalaxy": "GALAXY_23",
    "SnowCapsuleGalaxy": "GALAXY_27", "FishTunnelGalaxy": "GALAXY_05", "HeavensDoorGalaxy": ("GALAXY_24", 2),
    "TeresaMario2DGalaxy": "GALAXY_12", "OceanPhantomCaveGalaxy": "GALAXY_INTER", "KoopaBattleVs3Galaxy": "GALAXY_28",
    "PeachCastleGardenGalaxy": "PROLOGUE_05", "BattleShipGalaxy": ("GALAXY_11", 3), "CannonFleetGalaxy": ("GALAXY_11", 3),
    "PeachCastleFinalGalaxy": "PROLOGUE_02", "E3HeavenlyBeachGalaxy": "GALAXY_13",
    # AudStageBgmTable entry 0 (no sMAStageBgmData row)
    "StarDustGalaxy": "GALAXY_09", "IceVolcanoGalaxy": "GALAXY_INTER",
}
SCENARIO_BGM = [
    ["GALAXY_10", "GALAXY_10", "GALAXY_10_HURRY", "GALAXY_10", "GALAXY_10", "GALAXY_10"],
    ["ASTRO_OUT", "ASTRO_OUT_2", "ASTRO_OUT_3", "GALAXY_24", "ASTRO_OUT_3", "ASTRO_OUT_3"],
    [None, "GALAXY_24", "GALAXY_24", "GALAXY_24", "GALAXY_24", "GALAXY_24"],
    [None, None, None, None, "GALAXY_21", None],
    ["ASTRO_DOME", "ASTRO_DOME", "ASTRO_DOME", "ASTRO_DOME", "ASTRO_DOME", "ASTRO_DOME_LOFT"],
]
COMET_BGM = {"Red": "GALAXY_02_HURRY", "Dark": "GALAXY_02_HURRY", "Ghost": "GALAXY_20", "Quick": "GALAXY_20",
             "Black": "GALAXY_21"}  # AudStageBgmWrap::getCometEventBgm


# the demos' STM_ ids (StaffRoll::exePrepareBgm STM_STAFF_ROLL, the epilogue demo's sound sheet STM_EPILOGUE_B)
STM_BGM = {"STAFF_ROLL": "SMG_staffroll_strm", "EPILOGUE_B": "SMG_ev_epilogue_b_strm"}


def stream_of(bgm, streams):
    """Sound id name -> stream stem in AudioRes/Stream; a sequenced one (LIBRARY, ASTRO_DOME_LOFT...) -> its
    tools/bgmseq.py render SEQ_<name>; None: neither."""
    if "BGM_" + bgm in SEQ_BGMS:
        return "SEQ_" + bgm.lower()
    if bgm.startswith("GALAXY_"):
        stem = "SMG_" + bgm.lower().replace("galaxy_", "galaxy", 1).replace("galaxyinter", "galaxy_inter")
    elif bgm.startswith("ASTRO_OUT"):
        stem = "SMG_astroout0" + (bgm[-1] if bgm[-1].isdigit() else "1")
    elif bgm == "ASTRO_DOME":
        stem = "SMG_astrodome"
    elif bgm.startswith("PROLOGUE_"):
        stem = "SMG_ev_prolo" + bgm[-2:]
    else:
        return None
    return next((stem + t for t in ("_strm", "_multi") if stem + t in streams), None)


def sound_id(stem):
    """Stream stem -> sound id name (the inverse of stream_of)."""
    s = stem.replace("_strm", "").replace("_multi", "")
    if s.startswith("SMG_galaxy"):
        return "GALAXY_" + s[len("SMG_galaxy"):].lstrip("_").upper()
    if s.startswith("SMG_astroout0"):
        return "ASTRO_OUT" if s[-1] == "1" else "ASTRO_OUT_" + s[-1]
    if s == "SMG_astrodome":
        return "ASTRO_DOME"
    if s.startswith("SMG_ev_prolo"):
        return "PROLOGUE_" + s[-2:]
    return None


def scenario_bgm(stage, no, comet):
    """AudStageBgmWrap::changeStageNameToSoundID (no Koopa fortress / AstroGalaxy bright flag: a first visit)."""
    if comet in COMET_BGM:
        return COMET_BGM[comet]
    e = STAGE_BGM.get(stage)
    if isinstance(e, tuple):
        return SCENARIO_BGM[e[1]][no - 1] if no <= 6 and SCENARIO_BGM[e[1]][no - 1] else e[0]
    return e


def table(fs, out):
    """C table {"<Galaxy>/<n>", stream} for every scenario with a streamed BGM; returns the streams used."""
    streams = {p.name[:-4] for p in (Path(fs) / "AudioRes" / "Stream").glob("*.ast")}
    rows, used = [], set()
    for arc in sorted((Path(fs) / "StageData").glob("*/*Scenario.arc")):
        stage = arc.parent.name
        sd = {n.lower(): b for n, b in rarc_files(arc.read_bytes())}["scenariodata.bcsv"]
        for no, comet in zip(bcsv_column(sd, "ScenarioNo"), bcsv_column(sd, "Comet") or [""] * 99):
            bgm = scenario_bgm(stage, no, comet)
            st = bgm and stream_of(bgm, streams)
            if st:
                rows.append(f'    {{"{stage}/{no}", "{st}"}},')
                used.add(st)
    bgm = sorted((sound_id(st), st) for st in used if not st.startswith("SEQ_"))  # the demos' startStageBGM(name): sound id -> stream
    assert all(stream_of(i, streams) == st for i, st in bgm), bgm
    bgm += sorted((i, st) for i, st in STM_BGM.items() if st in streams)
    bgm += [(n[4:], "SEQ_" + n[4:].lower()) for n in SEQ_BGMS]  # the sequenced (bgm_sub, the jingles)
    used |= {st for _, st in bgm}
    Path(out).write_text("\n".join(["/* written by tools/music.py --table: scenario -> music stream (Petari AudStageBgmWrap) */",
                                    "static const struct { const char *scen, *track; } music_tab[] = {", *rows, "};",
                                    "/* sound id (MBGM_ dropped) -> music stream, for MR::startStageBGM by name */",
                                    "static const struct { const char *bgm, *track; } bgm_tab[] = {",
                                    *[f'    {{"{i}", "{st}"}},' for i, st in bgm], "};", ""]))
    print(f"{out}: {len(rows)} scenarios, {len(used)} streams, {len(bgm)} bgm ids")
    return used


def convert(ast):
    magic, size, fmt, bits, chans, _, rate, samples, loop_start, loop_end = struct.unpack_from(">4sIHHHHIIII", ast, 0)
    assert magic == b"STRM" and fmt == 1 and bits == 16, (magic, fmt, bits)
    out, o = [], 0x40
    ast = ast + bytes(0x10000)  # some last blocks end short of their size (galaxy03_multi): zeros past `samples`
    while o + 0x20 <= len(ast) and ast[o:o + 4] == b"BLCK":
        n = struct.unpack_from(">I", ast, o + 4)[0]
        o += 0x20
        chs = [np.frombuffer(ast, ">i2", n // 2, o + c * n) for c in range(chans)]
        o += n * chans
        out.append(np.stack(chs[:2] if chans >= 2 else [chs[0], chs[0]], axis=1))
    pcm = np.concatenate(out)[:samples].astype("<i2")
    return rate, loop_start * 4, pcm.tobytes()


def main(fs, out, names):
    src = Path(fs) / "AudioRes" / "Stream"
    names = names or table(fs, Path(__file__).resolve().parent.parent / "engine/src/music_tab.h")
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for p in sorted(src.glob("*.ast")):
        name = p.stem.replace(".ast", "")
        if names and name not in names:
            continue
        rate, loop, data = convert(p.read_bytes())
        (out / f"{name}.pcm").write_bytes(struct.pack("<4sIII", b"SMGM", rate, 2, loop) + data)
        print(f"{name}: {rate} Hz, {len(data) / 4 / rate:.1f} s, loop at {loop / 4 / rate:.1f} s, {len(data) >> 10} KB")


def selftest():
    # two blocks of 2 samples, 2 channels: L = 1, 2 | 5, 6 ; R = 3, 4 | 7, 8
    hdr = struct.pack(">4sIHHHHIIIII", b"STRM", 0, 1, 16, 2, 0xFFFF, 32000, 4, 1, 4, 4)
    hdr += b"\0" * (0x40 - len(hdr))
    blk = lambda l, r: b"BLCK" + struct.pack(">I", 4) + b"\0" * 0x18 + struct.pack(">2h", *l) + struct.pack(">2h", *r)
    rate, loop, data = convert(hdr + blk((1, 2), (3, 4)) + blk((5, 6), (7, 8)))
    assert rate == 32000 and loop == 4 and np.frombuffer(data, "<i2").tolist() == [1, 3, 2, 4, 5, 7, 6, 8]
    streams = {"SMG_galaxy11_strm", "SMG_galaxy21_strm", "SMG_galaxy_inter_strm", "SMG_galaxy01_tomb_strm",
               "SMG_galaxy03_multi", "SMG_astroout02_strm", "SMG_galaxy02_hurry_strm"}
    got = lambda *a: stream_of(scenario_bgm(*a), streams)
    assert got("BattleShipGalaxy", 1, "") == "SMG_galaxy11_strm" and got("BattleShipGalaxy", 5, "") == "SMG_galaxy21_strm"
    assert got("PhantomGalaxy", 2, "") == "SMG_galaxy_inter_strm" and got("TriLegLv1Galaxy", 1, "") == "SMG_galaxy01_tomb_strm"
    assert got("OceanRingGalaxy", 1, "") == "SMG_galaxy03_multi" and got("AstroGalaxy", 2, "") == "SMG_astroout02_strm"
    assert got("EggStarGalaxy", 4, "Dark") == "SMG_galaxy02_hurry_strm" and scenario_bgm("Nowhere", 1, "") is None
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif sys.argv[1] == "--table":
        table(sys.argv[2], sys.argv[3])
    else:
        main(sys.argv[1], sys.argv[2], sys.argv[3:])
