"""Run slice.py over every galaxy/scenario: blob size per scenario, failures, missing object classes.

usage: python tools/slice_all.py data/fs/files out/slice_all [jobs]
writes <out>/<Galaxy>_<n>.bin and <out>/scenarios.csv (galaxy, scenario, bytes, models, textures, error)
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


def scenarios(fs):
    for g in sorted((Path(fs) / "StageData").iterdir()):
        arc = g / f"{g.name}Scenario.arc"
        if g.is_dir() and arc.exists():
            sd = {n.lower(): d for n, d in B.rarc_files(arc.read_bytes())}["scenariodata.bcsv"]
            for no in sorted(B.scenario_masks(sd)):
                yield g.name, no


def one(fs, out, galaxy, no):
    t = time.time()
    try:
        blob, info = SL.build(fs, galaxy, no)
        (Path(out) / f"{galaxy}_{no}.bin").write_bytes(blob)
        return dict(galaxy=galaxy, scenario=no, bytes=len(blob), seconds=round(time.time() - t, 1),
                    zones=len(info["stages"]), objs=info["objs"], models=info["models"], textures=info["textures"],
                    error="")
    except Exception as e:
        return dict(galaxy=galaxy, scenario=no, bytes=0, seconds=round(time.time() - t, 1), zones=0, objs=0, models=0,
                    textures=0, error=f"{type(e).__name__}: {e} @ {traceback.extract_tb(e.__traceback__)[-1].lineno}")


if __name__ == "__main__":
    fs, out = sys.argv[1], Path(sys.argv[2])
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 6
    out.mkdir(parents=True, exist_ok=True)
    todo = list(scenarios(fs))
    rows = []
    with ProcessPoolExecutor(jobs) as ex:
        for r in ex.map(one, *zip(*[(fs, str(out), g, n) for g, n in todo])):
            rows.append(r)
            print(f"{r['galaxy']} {r['scenario']}: {r['bytes'] / 1e6:.2f} MB {r['seconds']} s {r['error']}", flush=True)
    rows.sort(key=lambda r: -r["bytes"])
    with open(out / "scenarios.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
    ok = [r for r in rows if not r["error"]]
    print(f"{len(ok)} / {len(rows)} built, largest {ok[0]['galaxy']} {ok[0]['scenario']} {ok[0]['bytes'] / 1e6:.2f} MB")
