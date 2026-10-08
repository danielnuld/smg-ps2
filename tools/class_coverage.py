"""Class coverage for the task 5.3 gate: every placed class (out/census.json placements) as
  behaviour  the engine runs it (named in engine/src/game.c: scene_type / strcmp / count_type / strncmp, gravities in
             gravity.c's kinds, planets),
  static     no behaviour, but its model is drawn and collided as a static instance,
  missing    neither (no model on disc and no code: logic objects, effects, areas),
weighted by placement count. usage: python tools/class_coverage.py [fs, default data/fs/files] -> out/class_coverage.md
"""
import json
import re
import sys
from pathlib import Path

import budget as B
import slice as S

ROOT = Path(__file__).parent.parent


def engine_classes():
    src = (ROOT / "engine/src/game.c").read_text(encoding="utf-8")
    names = set(re.findall(r'(?:scene_type\(s, |strcmp\([a-z>-]+, |count_type\(|strncmp\(name, )"([A-Za-z0-9]+)"', src))
    prefixes = set(re.findall(r'strncmp\(name, "([A-Za-z0-9]+)", \d+\)', src))
    return names, prefixes


def classify(fs):
    place = json.loads((ROOT / "out/census.json").read_text())["place"]
    fsp = Path(fs)
    planets = B.planet_table(fsp, lambda a: (fsp / "ObjectData" / f"{a}.arc").exists())
    names, prefixes = engine_classes()
    out = []
    for cls, n in place.items():
        if cls in names or any(cls.startswith(p) for p in prefixes) or "Gravity" in cls:
            kind = "behaviour"
        elif cls in planets:
            kind = "behaviour"  # planets: drawn and collided, that is all they do
        elif cls in S.NO_DRAW:
            kind = "missing"
        elif S.model_archive(fsp, cls, planets):
            kind = "static"
        else:
            kind = "missing"
        out.append((cls, n, kind))
    return sorted(out, key=lambda r: -r[1])


def main():
    fs = sys.argv[1] if len(sys.argv) > 1 else "data/fs/files"
    rows = classify(fs)
    tot = sum(n for _, n, _ in rows)
    by = {k: sum(n for _, n, kk in rows if kk == k) for k in ("behaviour", "static", "missing")}
    lines = ["# Class coverage (placements, all scenarios)", "",
             "| kind | classes | placements | share |", "|---|---|---|---|"]
    for k, v in by.items():
        lines.append(f"| {k} | {sum(1 for r in rows if r[2] == k)} | {v} | {100 * v / tot:.1f} % |")
    for k in ("static", "missing"):
        lines += ["", f"## Top {k} classes", "", "| class | placements |", "|---|---|"]
        lines += [f"| {c} | {n} |" for c, n, kk in rows if kk == k][:40]
    (ROOT / "out/class_coverage.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:7]))


if __name__ == "__main__":
    main()
