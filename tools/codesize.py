"""Task 4.2: EE (MIPS) vs. Wii (PPC) code size on the TUs that compiled for the EE.

usage: python tools/codesize.py     (after `wsl bash tools/ee_codesize.sh`)
PPC per-TU ranges: Petari config/RMGK01/splits.txt. MIPS: `size -A` of the `ld -r` of every compiled TU.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPLITS = ROOT / "vendor/Petari/config/RMGK01/splits.txt"
PORTED = ("Game", "JSystem", "nw4r", "RVLFaceLib")  # SDK/MSL/TRK/Runtime get replaced by ps2sdk + newlib
GROUP = {".text": "code", ".rodata": "const", ".sdata2": "const", ".sbss2": "const",
         ".data": "data", ".sdata": "data", ".ctors": "data", ".dtors": "data", ".bss": "bss", ".sbss": "bss"}

def ppc_tus():
    tus, cur = {}, None
    for line in SPLITS.read_text().splitlines():
        if re.match(r"^\S.*\.(c|cpp|cp|s)?:$", line) and not line.startswith("Sections"):
            cur = tus.setdefault(line[:-1], dict(code=0, const=0, data=0, bss=0))
        elif cur is not None and (m := re.match(r"\s+(\.\w+)\s+start:0x(\w+) end:0x(\w+)", line)) and m[1] in GROUP:
            cur[GROUP[m[1]]] += int(m[3], 16) - int(m[2], 16)
    return tus

def mips_sizes():
    out = dict(code=0, const=0, data=0, bss=0)
    for line in (ROOT / "out/ee/all_size.txt").read_text().splitlines():
        m = re.match(r"(\.\S+)\s+(\d+)", line)
        if not m:
            continue
        s, n = m[1], int(m[2])
        g = ("code" if s.startswith(".text") else "const" if s.startswith(".rodata") else
             "bss" if s.startswith((".bss", ".sbss")) else "data" if s.startswith((".data", ".sdata", ".ctors", ".init_array")) else None)
        if g:
            out[g] += n
    return out

if __name__ == "__main__":
    tus = ppc_tus()
    ok = [l.strip()[len("src/"):] for l in (ROOT / "out/ee/ok.txt").read_text().splitlines() if l.strip()]
    fail = [l.strip()[len("src/"):] for l in (ROOT / "out/ee/fail.txt").read_text().splitlines() if l.strip()]
    ported = {k: v for k, v in tus.items() if k.split("/")[0] in PORTED}
    S = lambda names, g: sum(tus[n][g] for n in names if n in tus)
    M = lambda x: f"{x / 2**20:.2f} MB"
    mips = mips_sizes()
    missing = [n for n in ok if n not in tus]
    print(f"TUs: {len(ok)} compiled, {len(fail)} failed, {len(missing)} compiled but not in splits")
    for g in ("code", "const", "data", "bss"):
        ppc_ok, ppc_all = S(ok, g), sum(v[g] for v in ported.values())
        ratio = mips[g] / ppc_ok if ppc_ok else float("nan")
        print(f"{g:5}  ppc(compiled TUs) {M(ppc_ok)}  mips {M(mips[g])}  ratio {ratio:.2f}"
              f"  | ppc all ported {M(ppc_all)} -> mips extrapolated {M(ppc_all * ratio)}")
