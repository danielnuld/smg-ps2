"""Hand-cut sound-effect set per scenario: which SE names a scenario can need, from Petari's code and placement.

Kept for every scenario (D, the cut rules):
  - whole categories that follow Mario everywhere: PLAYER_VOICE, PLAYER_MOTION, REMIX_SEQ;
  - SYSTEM only where code names the sound (like every other category);
  - SE names written in global code (GLOBAL_DIRS of vendor/Petari/src/Game).
Kept only where placed:
  - SE names written in the .cpp files of the classes the scenario's placed objects create (NameObjFactory
    cCreateTable: name -> createX< Class >), plus every class those files `new` (transitive);
  - StageEffectDataTable sounds keyed by a placed object name.
  - sounds named by the animation sound tables (.bas) inside the ObjectData archives the scenario loads
    (JAUSoundAnimator.hpp:96-121,153-155: u16 count, 0x20-byte entries from +8, word 0 = sound id
    {section u8, group u8, index u16}; checked: Kuribo walk.bas -> SE_EM_KURIBO_STEP1/2).
Waves no sequence plays (unused key regions) are dropped. 23 SE sequences have notes whose bank/program the
sequence does not fix (25 notes); their waves cannot be named and are not kept (listed by `unknown_bank()`).
Dropped: HOME_BUTTON_MENU (Wii only) and any SE name no code or table above names (DUMMY entries, sounds built from
strings at run time, sounds only reached through data). The dropped names are listed, so the cut is reviewable.
ponytail: classes are matched to files by `Class::` definitions and `new Class` uses; a sound played by a helper
reached some other way (templates, function tables) is dropped. Upgrade path: log played SE ids in Dolphin.
"""
import re
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "vendor/Petari/src/Game"
KEEP_CATEGORIES = {"PLAYER_VOICE", "PLAYER_MOTION", "REMIX_SEQ"}
DROP_CATEGORIES = {"HOME_BUTTON_MENU",   # Wii HOME menu, no PS2 counterpart
                   "DEMO"}               # cutscene sounds ride in the cutscene's pre-rendered stream, like music
# max sample rate per category (D); a wave shared by several kept sounds keeps the highest cap among them
RATE_CAP = {"SYSTEM": 8000, "PLAYER_VOICE": 8000, "PLAYER_MOTION": 8000, "REMIX_SEQ": 8000}
DEFAULT_CAP = 6000
ONE_VARIANT = True  # a note whose key comes from a register (random variant) keeps only its program's first region
# Map/, Ride/ and AreaObj/ hold placeable classes (WaterRoad, Flag, SpinDriver, SurfRay...): their sounds count only
# where the class is placed, like Enemy/ or MapObj/.
GLOBAL_DIRS = {"Player", "Screen", "System", "Scene", "Camera", "Util", "AudioLib", "Demo", "LiveActor", "Effect",
               "Gravity", "NameObj", "Speaker", "RhythmLib"}
SE_RE = re.compile(r'"((?:SE|HOMESE)_[A-Z0-9_]+)"')


def code_index():
    """-> (sounds per file, files defining each class, classes each file news, StageEffect {object: sounds})."""
    sounds, defines, news = {}, {}, {}
    for f in SRC.rglob("*.cpp"):
        t = f.read_text(encoding="utf-8", errors="replace")
        rel = f.relative_to(SRC).as_posix()
        sounds[rel] = set(SE_RE.findall(t))
        for c in set(re.findall(r"\b([A-Z]\w+)::\1\s*\(", t)):          # constructor definition = class lives here
            defines.setdefault(c, set()).add(rel)
        news[rel] = set(re.findall(r"\bnew\s+(?:\([^)]*\)\s*)?([A-Z]\w+)\b", t))
    stage_fx = {}
    t = (SRC / "MapObj/StageEffectDataTable.cpp").read_text(encoding="utf-8")
    body = t[t.index("sStageEffectSeDataTable[]"):]
    body = body[:body.index("};")]
    for name, rest in re.findall(r'\{\s*"([^"]+)",([^}]*)\}', body):
        stage_fx.setdefault(name, set()).update(re.findall(r'"(SE_[A-Z0-9_]+)"', rest))
    return sounds, defines, news, stage_fx


def create_classes():
    """NameObjFactory cCreateTable -> {placement name lower: class} (template argument of the create function)."""
    t = (SRC / "NameObj/NameObjFactory.cpp").read_text(encoding="utf-8")
    body = t[t.index("cCreateTable[]"):t.index("cName2ArchiveNamesTable[]")]
    out = {}
    for name, func in re.findall(r'\{\s*"([^"]+)",\s*([^,]+?),\s*(?:nullptr|"[^"]*"),?\s*\}', body, re.S):
        m = re.search(r"<\s*([\w:]+)\s*>", func)
        if m:
            out[name.lower()] = m.group(1).split("::")[-1]
    return out


def bas_ids(b):
    """-> [(group, index)] of the section-0 (SE) sounds in a .bas file."""
    import struct
    if len(b) < 8:
        return []
    n = struct.unpack_from(">H", b, 0)[0]
    out = []
    for i in range(n):
        o = 8 + 0x20 * i
        if o + 4 > len(b):
            break
        sid = struct.unpack_from(">I", b, o)[0]
        if sid >> 24 == 0:
            out.append((sid >> 16 & 0xFF, sid & 0xFFFF))
    return out


class Cutter:
    def __init__(self):
        self.sounds, self.defines, self.news, self.stage_fx = code_index()
        self.classes = create_classes()
        self.global_names = set().union(*(s for f, s in self.sounds.items() if f.split("/")[0] in GLOBAL_DIRS))

    def files_for(self, placed):
        """Placed object names -> source files of their classes and of every class those files `new`."""
        todo = [self.classes[n.lower()] for n in placed if n.lower() in self.classes]
        seen_c, files = set(), set()
        while todo:
            c = todo.pop()
            if c in seen_c:
                continue
            seen_c.add(c)
            for f in self.defines.get(c, ()):
                if f not in files:
                    files.add(f)
                    todo += self.news.get(f, ())
        return files

    def keep(self, placed, se_names, extra=()):
        """-> set of SE names kept for a scenario with these placed object names (+ extra names, e.g. from .bas).
        se_names: audio.se_names()."""
        names = set(self.global_names) | set(extra)
        for f in self.files_for(placed):
            names |= self.sounds[f]
        for n in placed:
            names |= self.stage_fx.get(n, set())
        out = set()
        for cat, ns in se_names:
            if cat in DROP_CATEGORIES:
                continue
            out |= set(ns) if cat in KEEP_CATEGORIES else names & set(ns)
        return out


def main(fs, sets_json, detail=None, cutlist=None):
    """Per scenario: SPU2 bytes of the waves its kept SE names reach, within the banks the scene loads."""
    import json, struct, sys
    sys.path.insert(0, str(Path(__file__).parent))
    import audio as A
    from budget import rarc_files, yaz0, u32
    smr = yaz0((Path(fs) / "AudioRes/SMR.szs").read_bytes())
    inst, names = A.instruments(smr), A.se_names(smr)
    bgm, se = A.sequences(fs, smr)
    flat = [(c, n) for c, ns in names for n in ns]
    A.DYN_KEY = "first" if ONE_VARIANT else "all"
    per_name = {n: A.reached([q], inst)[0] for q, (c, n) in zip(se, flat)}
    cat_of = {n: c for c, n in flat}
    info = {}                                       # (bank, wave id, archive) -> (rate, samples)
    for bid, o in A.cmds(smr, b"ws  ", b"WSYS"):
        winf, grp = o + u32(smr, o + 0x10), o + u32(smr, o + 0x14)
        for i in range(u32(smr, winf + 4)):
            ar = o + u32(smr, winf + 8 + 4 * i)
            ctrl = o + u32(smr, o + u32(smr, grp + 0xC + 4 * i) + 0xC)
            for j in range(u32(smr, ar + 0x70)):
                w = o + u32(smr, ar + 0x74 + 4 * j)
                wid = u32(smr, o + u32(smr, ctrl + 8 + 4 * j)) & 0xFFFF
                info[(bid, wid, i)] = (struct.unpack_from(">f", smr, w + 4)[0], u32(smr, w + 0x1C))
    scene = A.scene_sets()
    base = {(b, None) for b in A.STATIC + A.SYSTEM} | {A.PLAYER}
    cut, bas_cache, rows, kept_in = Cutter(), {}, [], {}
    for key, s in json.load(open(sets_json)).items():
        gal, no = key.split(":")
        bas = set()
        for p in s["archives"]:
            if p not in bas_cache:
                bas_cache[p] = {names[g][1][k] for n, b in rarc_files(Path(p).read_bytes()) if n.lower().endswith(".bas")
                                for g, k in bas_ids(b) if g < len(names) and k < len(names[g][1])}
            bas |= bas_cache[p]
        kept = cut.keep(set(s["names"]), names, bas)
        for n in kept:
            kept_in[n] = kept_in.get(n, 0) + 1
        cap = {}
        for n in kept:
            c = RATE_CAP.get(cat_of[n], DEFAULT_CAP)
            for w in per_name[n]:
                cap[w] = max(cap.get(w, 0), c)
        waves = set(cap)
        loaded = scene.get((gal, int(no)), scene.get((gal, 0), base))
        owner = {}
        for n in kept:
            for w in per_name[n]:
                owner.setdefault(w, cat_of[n])
        def size(rules, by=None):
            t = 0
            for (bid, wid, i), (r, n) in info.items():
                if not any(bb == bid and (idx is None or idx == i) for bb, idx in loaded):
                    continue
                if (bid, wid) not in waves:
                    continue                        # dropped
                c = cap[(bid, wid)] if rules else None
                if c and r > c:
                    n = int(n * c / r)
                t += -(-n // 28) * 16
                if by is not None:
                    by[owner[(bid, wid)]] = by.get(owner[(bid, wid)], 0) + -(-n // 28) * 16
            return t
        if key == detail:
            by = {}
            size(True, by)
            print(f"{key} with the rules, by first kept category:")
            for c, v in sorted(by.items(), key=lambda kv: -kv[1]):
                print(f"  {c:18} {v / 2**20:.2f} MB")
        rows.append((key, len(kept), size(False), size(True)))
    if cutlist:
        import csv
        with open(cutlist, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["category", "sound", "kept_in_scenarios", "rule", "max_rate_hz"])
            for c, n in flat:
                k = kept_in.get(n, 0)
                rule = ("dropped: category" if c in DROP_CATEGORIES else "kept: whole category" if c in KEEP_CATEGORIES
                        else "kept: global code" if n in cut.global_names else "kept: placed class / .bas / stage table"
                        if k else "dropped: no known user")
                w.writerow([c, n, k, rule, RATE_CAP.get(c, DEFAULT_CAP) if k else ""])
    rows.sort(key=lambda r: -r[3])
    M = 2**20
    print(f"{'scenario':32} {'SE kept':>7} {'orig MB':>8} {'rules MB':>9}")
    for k, n, a, b in rows[:12]:
        print(f"{k:32} {n:7} {a / M:8.2f} {b / M:9.2f}")
    print(f"scenarios over 1.86 MB with the rules: {sum(r[3] > 1950336 for r in rows)} / {len(rows)}")
    return rows


def unknown_bank(fs):
    """SE names with notes whose bank or program the sequence leaves unset or reads from a register."""
    import sys
    sys.path.insert(0, str(Path(__file__).parent))
    import audio as A, bms
    from budget import yaz0
    smr = yaz0((Path(fs) / "AudioRes/SMR.szs").read_bytes())
    names = [n for _, ns in A.se_names(smr) for n in ns]
    return [n for (d, st), n in zip(A.sequences(fs, smr)[1], names)
            if any(bk is None or bk == bms.DYN or pg == bms.DYN for bk, pg in bms.analyse(d, st)["pairs"])]


def selftest():
    c = Cutter()
    assert c.classes["kuribo"] == "Kuribo" and "Enemy/Kuribo.cpp" in c.defines["Kuribo"], c.defines.get("Kuribo")
    se = [("PLAYER_VOICE", ["SE_PV_X"]), ("ENEMY_MOTION", ["SE_EM_KURIBO_FOOTNOTE", "SE_EM_NOT_HERE"]),
          ("HOME_BUTTON_MENU", ["HOMESE_HOME_BUTTON"])]
    k = c.keep({"Kuribo"}, se)
    assert "SE_PV_X" in k and "HOMESE_HOME_BUTTON" not in k and "SE_EM_NOT_HERE" not in k, k
    assert ("SE_EM_KURIBO_FOOTNOTE" in k) == ("SE_EM_KURIBO_FOOTNOTE" in set().union(
        *(c.sounds[f] for f in c.files_for({"Kuribo"}))) | c.global_names), k
    assert "SE_EM_KURIBO_FOOTNOTE" not in c.keep(set(), se) or "SE_EM_KURIBO_FOOTNOTE" in c.global_names
    assert bas_ids(bytes.fromhex("0002000000000000") + bytes.fromhex("00090008") + bytes(28)
                   + bytes.fromhex("00090009") + bytes(28)) == [(9, 8), (9, 9)]
    print("selftest ok", len(c.classes), "create entries,", len(c.global_names), "global SE names")


if __name__ == "__main__":
    import sys
    if sys.argv[1:] in ([], ["--selftest"]):
        selftest()
    else:  # sfxcut.py <fs> <scenario_sets.json> [detail scenario] [cut list csv]
        main(sys.argv[1], sys.argv[2], *(a or None for a in sys.argv[3:5]))
