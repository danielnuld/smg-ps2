"""Sound gaps: the SE names Petari's code plays that the port's engine never names, per galaxy (placed classes) and
in global code (Mario's playSound keys, HUD / screens / talk), plus Petari's BGM / ME calls -> docs/sound-gaps.md.

Petari side as tools/sfxcut.py (placement -> class -> its files and every class they `new`, helper files sharing
the class name's start, StageEffectDataTable by object name; .bas tables left out: the engine plays them from the
packs). Port side: every SE_ name in engine/src/*.c and *.inc and Mario's table names behind the keys mario_sound() plays.
A name counts as ported if the engine names it anywhere (a name shared by two classes hides the second's gap).

usage (cwd the repo): python tools/sndgaps.py [docs/sound-gaps.md]
"""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, "tools")  # cwd the main repo: its vendor/Petari (a worktree has none)
import sfxcut as C  # noqa: E402
from sound import scene_types  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PACKS = Path("out/packs")
# Names Petari's code holds but the game never plays (read from the code: a nerve nothing sets, an unused string), or
# plays only for the Wii's second player (the co-star pointer): not gaps.
NOT_PLAYED = {
    "SE_EM_KURIBOCHIEF_TRAMPLE": "KuriboChief's Trample nerve is never set",
    "SE_EM_KARIKARI_CRUSH_CRYSTAL_L": "Karikari's Release nerve is never set (so no SpinAttacked / Frozen)",
    "SE_EM_KARIKARI_CRUSH_CRYSTAL_M": "Karikari's Release nerve is never set (FrozenRecover)",
    "SE_EM_KARIKARI_FREEZE_CRYSTAL": "Karikari's Release nerve is never set (Frozen)",
    "SE_EM_LV_KARIKARI_TORNADO": "Karikari's InTornado nerve is never set",
    "SE_EM_BOMBHEI_HIT": "BombHei's PursueHit nerve is never set",
    "SE_BV_BOSS_BUG_ANGRY_LAND": "BossStinkBugActionFlyHigh's ToGround nerve is never set",
    "SE_BV_BOSS_BUG_ANGRY_TAKEOFF": "BossStinkBugActionFlyHigh's ToFly nerve is never set",
    "SE_BV_POLTA_SHOUT": "an unused string (PoltaActionBase.cpp poltaShoutUnused)",
    "SE_EM_BOMBTERE_BOUND": "BombTeresa's BallAppear nerve is never set",
    "SE_OJ_TSPIDER_COCOON_BOUND": "TombSpiderActionCocoon: only for a sender that is no ATYPE_SPRING_ATTACKER, and "
                                  "SlingShooter's attack sensor (that type) is the only sender of the message",
    "SE_EM_FIRE_BUBBLE_REFLECT": "the second player's pointer (FireBubble / FireBall Reflect, KoopaFireShort Erase2P)",
    "SE_OJ_SPACEMINE_APPEAR": "SpaceMine's Appear nerve is never set",
    "SE_OJ_LV_SPACEMINE_APPEAR": "SpaceMine's Appear nerve is never set",
    "SE_SM_RABBIT_HOP": "Rabbit's rail behaviour (arg1 0: PreJump / PreJumpBack) has no placed row",
    "SE_SM_RABBIT_CHANGE_JUMP": "Rabbit's rail behaviour (arg1 0: Finish) has no placed row",
    "SE_SM_RABBIT_CHANGE_EFFECT": "Rabbit's rail behaviour (arg1 0: Finish) has no placed row",
    "SE_OJ_SMN_VOLC_COL_S_BREAK": "SubmarineVolcanoSmallColumn is placed nowhere",
    "SE_OJ_PLANT_MARIO_UP_START": "only stopped (Plant.cpp stopSound), never started",
    "SE_OJ_CRYSTAL_CAGE_S_DAMAGE": "only CrystalCageL takes two hits (_C4 2), and it plays ..._L_DAMAGE",
    "SE_OJ_SANDSTORM_BIND_END": "Sandstorm::exeStormEnd tests the member function's address (always true): ..._S_BIND_END",
}


def mario_keys_in(src):
    """the keys mario_sound() plays: every string on a line that calls it (ternaries too)"""
    return {k for line in src.splitlines() if "mario_sound(" in line for k in re.findall(r'"([^"]+)"', line)}


def port_names():
    src = "".join(p.read_text("utf-8", "ignore") for p in sorted((ROOT / "engine/src").glob("*.c")) + sorted((ROOT / "engine/src").glob("*.inc")))
    names = set(re.findall(r"\bSE_[A-Z0-9_]+\b", src))
    keys = mario_keys_in(src)
    table = re.findall(r'\{"([^"]+)", (SE_[A-Z0-9_]+), \d+, (?:"[^"]*"|NULL)\}',
                       (ROOT / "engine/src/mario_snd.h").read_text("utf-8"))
    names |= {se for k, se in table if k in keys or (k.startswith("声") and k[1:] in keys)}
    return names, keys, src


def mario_keys():
    """playSound("key") in Petari's Player code -> {key: [file:line]}"""
    out = defaultdict(list)
    for f in sorted((C.SRC / "Player").glob("*.cpp")):
        for i, line in enumerate(f.read_text("utf-8", "replace").splitlines(), 1):
            for k in re.findall(r'playSound\("([^"]+)"', line):
                out[k].append(f"{f.name}:{i}")
    return out


def bgm_calls():
    """Petari's BGM / ME calls (MR:: sound utilities) -> [(function, args, file)]"""
    pat = re.compile(r"MR::(start\w*(?:BGM|Bgm|ME)\w*|stop\w*(?:BGM|Bgm)\w*|setStageBGM\w*|\w*BgmVolume\w*|"
                     r"moveVolume\w*BGM\w*|\w*SubBGM\w*|startStageBGM)\s*\(([^;]*)\)")
    out = []
    for f in sorted(C.SRC.rglob("*.cpp")):
        if f.parts[-2] in ("AudioLib", "Util"):
            continue
        for m in pat.finditer(f.read_text("utf-8", "replace")):
            out.append((m.group(1), m.group(2).strip(), f.relative_to(C.SRC).as_posix()))
    return out


def main(out_md="docs/sound-gaps.md"):
    cut = C.Cutter()
    have, keys, src = port_names()
    have |= set(NOT_PLAYED)
    gal = defaultdict(set)  # galaxy -> placed names (every scenario)
    for sb in sorted(PACKS.glob("*/*/scene.bin")):
        gal[sb.parts[-3]] |= scene_types(sb)
    files_of = {}

    def cls_files(n):
        if n not in files_of:
            c = cut.classes.get(n.lower())
            fs = cut.files_for({n})
            if c:
                fs |= {f for f in cut.sounds if f.rsplit("/", 1)[-1].startswith(c)}
            files_of[n] = fs
        return files_of[n]

    rows, cls_gal, cls_miss = [], Counter(), {}
    for g, placed in sorted(gal.items()):
        want = set()
        for n in placed:
            ns = set().union(*(cut.sounds[f] for f in cls_files(n))) | cut.stage_fx.get(n, set())
            ns -= cut.global_names
            miss = ns - have
            if ns:
                cls_gal[n] += 1
                cls_miss[n] = (sorted(miss), len(ns), f'"{n}"' in src)
            want |= ns
        rows.append((g, len(placed), len(want), len(want - have)))
    glob_miss = defaultdict(set)
    for f, ns in cut.sounds.items():
        if f.split("/")[0] in C.GLOBAL_DIRS and f.split("/")[0] != "AudioLib":
            m = {n for n in ns if n.startswith("SE_")} - have
            if m:
                glob_miss[f] = m
    mk = mario_keys()
    table_keys = set(re.findall(r'\{"([^"]+)", SE_', (ROOT / "engine/src/mario_snd.h").read_text("utf-8")))
    mk_miss = {k: v for k, v in mk.items() if k not in keys}
    bgm = bgm_calls()
    port_bgm = Counter(re.findall(r"\b(bgm_\w+|game_bgm_\w+|audio_fade|audio_music)\(", src))

    L = ["# Sound gaps: Petari's sound calls the port does not make", "",
         "Generated by `python tools/sndgaps.py` (Petari side as tools/sfxcut.py: placed class -> its .cpp files and "
         "what they `new`, StageEffectDataTable; port side: SE_ names in engine/src/*.c / *.inc and Mario's table behind the "
         "keys mario_sound() plays). A name the engine names anywhere counts as ported. .bas animation sounds are "
         "not counted (draw_skin plays them from the packs).", ""]
    tw, tm = len(set().union(*(cut.sounds[f] for n in cls_miss for f in cls_files(n))) - cut.global_names), 0
    allmiss = set().union(*(set(v[0]) for v in cls_miss.values())) if cls_miss else set()
    gm = set().union(*glob_miss.values()) if glob_miss else set()
    L += ["## Headline", "",
          f"- Placement names whose classes have sound calls: {len(cls_miss)}; their distinct SE names: {tw}; not in the port: "
          f"{len(allmiss)}.",
          f"- Global code (Player, Screen, Scene, System, Util, Demo, ...): {len(gm)} SE names not in the port, in "
          f"{len(glob_miss)} files.",
          f"- Mario: {len(mk)} distinct playSound keys in Petari's Player code, {len(mk_miss)} never played by the "
          f"port (of {len(table_keys)} table keys).",
          f"- BGM / ME: {len(bgm)} MR:: BGM / ME calls in Petari outside AudioLib / Util; the port's BGM entry "
          f"points: {', '.join(f'{k} x{v}' for k, v in port_bgm.most_common())}.", ""]
    L += ["## BGM / ME behaviour (read by hand from the list at the end)", "",
          "- ME_* music effects: ported (engine/src/me.c, Petari's RhythmLib): MR::startSystemME plays while the music "
          "is a rhythm BGM (hasME: its .rhy from tools/rhythm.py gives the beat callbacks and the chord / scale).",
          "- Sequenced BGMs play as tools/bgmseq.py renders (SEQ_*): one stream at a time (a sub BGM takes the "
          "stream, the stage's place kept), no cross fade, one volume for the stage and sub BGM (audio_move_volume: "
          "moveVolumeStageBGM / SubBGM), no setSoundVolumeSetting.",
          "- Audited 2026-11-20, every BGM / ME id of the list at the end against engine/src: all named. The bosses' "
          "startBossBGM / stopStageBGM / startAfterBossBGM / setStageBGMState, the volume moves (the star's appear "
          "demo, TalkBalloon, NoteFairy, MechaKoopaPartsHead, the pinch BGM, AudEffectDirector), ChangeBgmCube, "
          "BgmProhibitArea, the dome's selector states, the demo sheets' BGM (DemoSoundKeeper), the picture book's "
          "STM_PROLOGUE_01 and Bowser's grand star fanfares (PowerStar::exeAppearDemoKoopa) are in. Not checked call "
          "by call: Dodoryu, Koopa, Polta, SkeletalFishBoss, TripodBoss, IceMerameraKing, KoopaJrShip, Tamakoro "
          "(their counts match), the timing of each call, and anything by ear.",
          ""]
    L += ["## Per galaxy", "", "| galaxy | placed names | class SE names | missing |", "|---|---:|---:|---:|"]
    L += [f"| {g} | {p} | {w} | {m} |" for g, p, w, m in sorted(rows, key=lambda r: -r[3])]
    L += ["", "## Classes by galaxies placed (missing / all names; * = the port does not name the placement)", "",
          "| class | galaxies | missing | names missing |", "|---|---:|---:|---|"]
    for n, k in sorted(cls_gal.items(), key=lambda kv: (-kv[1] * len(cls_miss[kv[0]][0]), kv[0])):
        miss, tot, impl = cls_miss[n]
        if miss:
            L.append(f"| {n}{'' if impl else ' *'} | {k} | {len(miss)}/{tot} | {' '.join(miss[:12])}"
                     f"{' ...' if len(miss) > 12 else ''} |")
    L += ["", "## Names Petari never plays (not counted as gaps)", "", "| name | why |", "|---|---|"]
    L += [f"| {n} | {w} |" for n, w in sorted(NOT_PLAYED.items())]
    L += ["", "## Global code", "", "| file | missing SE names |", "|---|---|"]
    for f, m in sorted(glob_miss.items(), key=lambda kv: -len(kv[1])):
        L.append(f"| {f} | {' '.join(sorted(m))} |")
    L += ["", "## Mario's playSound keys the port never plays", "", "| key | Petari call sites |", "|---|---|"]
    for k, v in sorted(mk_miss.items(), key=lambda kv: -len(kv[1])):
        L.append(f"| {k} | {', '.join(v[:4])}{' ...' if len(v) > 4 else ''} |")
    L += ["", "## BGM / ME calls in Petari (outside AudioLib / Util)", "", "| call | args | file |", "|---|---|---|"]
    L += [f"| {fn} | `{a[:60]}` | {f} |" for fn, a, f in bgm]
    Path(out_md).write_text("\n".join(L) + "\n", "utf-8", newline="\n")
    print("\n".join(L[:14]))


if __name__ == "__main__":
    main(*sys.argv[1:2])
