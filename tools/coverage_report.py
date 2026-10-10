"""Task 5.3 gate table from out/coverage.csv (tools/coverage.ps1) and out/packs/scenarios.csv (tools/slice_all.py):
per scenario plays / fails / does not fit, streaming size, slow frames, worst frame; totals first.
usage: python tools/coverage_report.py -> out/coverage.md (and the totals on stdout)"""
import csv
from pathlib import Path

ROOT = Path(__file__).parent.parent


def main():
    sizes = {(r["galaxy"], r["scenario"]): r for r in csv.DictReader(open(ROOT / "out/packs/scenarios.csv"))}
    runs = {(r["galaxy"], r["scenario"]): r for r in csv.DictReader(open(ROOT / "out/coverage.csv"))}
    rows = []
    for key, s in sorted(sizes.items()):
        run = runs.get(key)
        state = "does not fit" if s["fits"] != "1" else "hidden" if s.get("start") == "0" else run["result"] if run else "not run"
        worst = int(run["worst_us"]) if run and run["worst_us"] else None
        rows.append((key, state, int(s["stream"]) / 2 ** 20, run["slow"] if run else "", worst))
    count = {k: sum(1 for r in rows if r[1] == k) for k in ("ok", "miss", "fail", "timeout", "does not fit", "hidden", "not run")}
    played = [r for r in rows if r[4] is not None]
    worst = max(played, key=lambda r: r[4]) if played else None
    lines = ["# Coverage (task 5.3)", "",
             f"Scenarios: {len(rows)}; plays {count['ok']}, misses {count['miss']}, fails {count['fail']}, timeout {count['timeout']}, "
             f"does not fit {count['does not fit']}, hidden stars {count['hidden']}, not run {count['not run']}.", ""]
    if worst:
        lines += [f"Worst frame after frame 60: {worst[4] / 1000:.1f} ms ({worst[0][0]} {worst[0][1]}); frames over "
                  f"20 ms after frame 120 summed over every run: {sum(int(r[3] or 0) for r in played)}.", ""]
    lines += ["| Galaxy | Scenario | Result | Streaming MB | Slow frames | Worst ms |", "|---|---|---|---|---|---|"]
    lines += [f"| {k[0]} | {k[1]} | {st} | {mb:.2f} | {slow} | {'' if w is None else f'{w / 1000:.1f}'} |"
              for k, st, mb, slow, w in rows]
    (ROOT / "out/coverage.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:5]))


if __name__ == "__main__":
    main()
