"""Convert every galaxy/scenario into the streaming layout (slice.py packs) and report the bytes per scenario.

usage: python tools/slice_all.py data/fs/files out/packs [jobs] [Galaxy,Galaxy...: only those repacked]
writes <out>/resident.bin (resident_luigi.bin: Luigi's), <out>/<Galaxy>/<n>/{scene,common,<Stage>,<Stage>.low}.bin and <out>/scenarios.csv:
  whole   every pack of the scenario in full (no streaming)
  stream  the most loaded at once with streaming (worst stage: its chain in full, other stages low)
  fits    stream <= ROOM
  start   1 if MarioNo 0 of zone 0 exists (a galaxy's entry; 0: a hidden star reached from another scenario)
"""
import csv
import struct
import sys
import time
import traceback
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B
import slice as SL

ROOM = 11.8e6  # room for scenario data and the menu packs beside it (main.c PACK_ROOM): the pack region less resident.bin, sound.bin's EE part and menus(3d).bin;
# measured 2026-11-07: 11415 KB (11.69e6) with a 17152 KB region (main.c "packs: ... region left N KB" + scene.bin + common.bin + map.bin,
# AstroGalaxy/1, resident.bin 4.46 MB) and 768 KB more since mem.c's arena went 6 -> 5.25 MB: ~0.65 MB of margin for ELF growth,
# a larger sound.bin EE part and resident_luigi.bin (tools/zone_fit.py --elf checks the real ELF)
# the menus' packs main.c loads beside a galaxy's own (scene_load: mp_names / mp_scene) come out of its room
# (mappics.bin is not one: menu.c reads one GalaxyDetail picture at a time into a 64 KB MEM_SCENE buffer, packs_read)
MENU_PACKS = {"FileSelect": ("title.bin", "title3d.bin"), "AstroDome": ("dome.bin",), "AstroGalaxy": ("map.bin",), "EpilogueDemoStage": ("ending.bin",),
              "PeachCastleGardenGalaxy": ("prologue.bin",), "LibraryRoom": ("picturebook.bin",)}
# and Luigi's letter (main.c letter_scene): in any galaxy while he is missing (MR::getLuigiLetterGalaxyName)
LETTER = "letter.bin"
RATIOS = (0.99, 0.9, 0.8, 0.7, 0.6, 0.5)  # static models' simplification, tried in turn until a scenario fits
# (tex.LAYER_MIN, tex.LAYER_MAX) tried in turn at each ratio: every layer, the larger ones, none
LAYER_STEPS = ((0.02, 2), (0.15, 1), (1.0, 0))
# the least ratio kept for layers: below it a scenario drops them first (the Observatory, AstroGalaxy, was at 0.7
# before them; layers were worth more there than 0.9's geometry: its metals and floors)
LAYER_RATIO = 0.7


def room(out, g):
    return ROOM - sum((Path(out) / f).stat().st_size for f in MENU_PACKS.get(g, ()) + (LETTER,) if (Path(out) / f).exists())


def has_start(scene):
    """1 if scene.bin has MarioNo 0 in zone 0, where the game enters a galaxy (Petari cInitializeStartIdInfo(0, 0)):
    hidden-star scenarios (layer mask 0: BattleShip 6, CosmosGarden 6) are only reached from another one."""
    i = scene.find(b"STRT")
    if i < 0:
        return 0
    n = struct.unpack_from("<I", scene, i + 16)[0]
    rows = (struct.unpack_from("<HHii9f8i", scene, i + 20 + 92 * k) for k in range(n))
    return int(any(r[1] == 0 and r[13] == 0 for r in rows))


def scenarios(fs, galaxy):
    sd = {n.lower(): d for n, d in B.rarc_files((Path(fs) / "StageData" / galaxy / f"{galaxy}Scenario.arc").read_bytes())}
    return sorted(B.scenario_masks(sd["scenariodata.bcsv"]))


def galaxy(fs, out, g, resident):
    rows = []
    gr = room(out, g)
    for no in scenarios(fs, g):  # one process per galaxy: its models are converted once (slice._model_cache)
        t = time.time()
        try:
            # over ROOM: fewer texture layers (tex.layers' passes), then its static models simplified further, down to
            # LAYER_RATIO; still over, no layers and the whole ladder again
            for ratio, (SL.tex.LAYER_MIN, SL.tex.LAYER_MAX) in (
                    [(r, s) for r in RATIOS if r >= LAYER_RATIO for s in LAYER_STEPS[:-1]] +
                    [(r, LAYER_STEPS[-1]) for r in RATIOS]):
                files, info = SL.packs(fs, g, no, resident, ratio)
                if max(info["stream"].values()) <= gr:
                    break
            d = Path(out) / g / str(no)
            d.mkdir(parents=True, exist_ok=True)
            for f, b in files.items():
                (d / f).write_bytes(b)
            worst = max(info["stream"], key=info["stream"].get)
            rows.append(dict(galaxy=g, scenario=no, whole=info["whole"], stream=info["stream"][worst], worst_stage=worst,
                             fits=int(info["stream"][worst] <= gr), stages=len(info["stages"]), objs=info["objs"],
                             start=has_start(files["scene.bin"]),
                             seconds=round(time.time() - t, 1), error=""))
        except Exception as e:
            rows.append(dict(galaxy=g, scenario=no, whole=0, stream=0, worst_stage="", fits=0, stages=0, objs=0, start=0,
                             seconds=round(time.time() - t, 1),
                             error=f"{type(e).__name__}: {e} @ {traceback.extract_tb(e.__traceback__)[-1].lineno}"))
        r = rows[-1]
        print(f"{g} {no}: whole {r['whole'] / 1e6:.2f} stream {r['stream'] / 1e6:.2f} MB ({r['worst_stage']}) "
              f"ratio {ratio} layers {SL.tex.LAYER_MAX} >= {SL.tex.LAYER_MIN} {r['seconds']} s {r['error']}", flush=True)
    return rows


if __name__ == "__main__":
    fs, out = sys.argv[1], Path(sys.argv[2])
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    out.mkdir(parents=True, exist_ok=True)
    res, names = SL.resident_pack(fs)
    (out / "resident.bin").write_bytes(res)
    print(f"resident.bin {len(res) / 1e6:.2f} MB ({', '.join(sorted(names))} + clips)", flush=True)
    lres, _ = SL.resident_pack(fs, player="Luigi")  # loaded instead when the file plays as Luigi (main.c)
    (out / "resident_luigi.bin").write_bytes(lres)
    print(f"resident_luigi.bin {len(lres) / 1e6:.2f} MB", flush=True)
    gs = sorted(p.name for p in (Path(fs) / "StageData").iterdir() if (p / f"{p.name}Scenario.arc").exists())
    if len(sys.argv) > 4:
        gs = [g for g in gs if g in sys.argv[4].split(",")]
    rows = []
    with ProcessPoolExecutor(jobs) as ex:
        for r in ex.map(galaxy, *zip(*[(fs, str(out), g, names) for g in gs])):
            rows += r
    rows.sort(key=lambda r: -r["stream"])
    with open(out / ("scenarios.csv" if len(sys.argv) <= 4 else "scenarios_some.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
    ok = [r for r in rows if not r["error"]]
    print(f"{len(ok)} / {len(rows)} converted, {sum(r['fits'] for r in ok)} fit {ROOM / 1e6:.2f} MB streaming, "
          f"{sum(r['whole'] <= ROOM for r in ok)} whole; worst {ok[0]['galaxy']} {ok[0]['scenario']} "
          f"{ok[0]['stream'] / 1e6:.2f} MB")
