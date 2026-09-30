"""Porting-debt counts for the EE, per top-level directory.

usage: python tools/audit.py vendor/galaxy-macos     (or vendor/Petari)
       python tools/audit.py --selftest
"""
import re, sys
from collections import Counter
from pathlib import Path

GX_NAME = re.compile(r"\b(GX[A-Z]\w*)\s*\(")
STRIP = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.S)
CATS = {
    # EE has no double FPU: every one of these is soft-float
    "double_decl": re.compile(r"\b(?:double|f64)\b"),
    "unsuffixed_float": re.compile(r"(?<![\w.])(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?(?![\w.])"),
    "double_math_call": re.compile(r"\b(?:sqrt|sin|cos|tan|atan2?|asin|acos|pow|fabs|floor|ceil|fmod|exp|log)\s*\("),
    # PowerPC-only code
    "ppc_asm": re.compile(r"\basm\b|__asm\b"),
    "paired_single": re.compile(r"__psq_\w+|\bps_\w+\s*\(|\bpsq_[ls]\w*"),
    # already handled by galaxy-macos (reusable work)
    "target_pc_blocks": re.compile(r"#\s*if(?:def)?\s+.*TARGET_PC"),
    "endian_sites": re.compile(r"\bBE\s*\(|PortReadBE\w*|PTR32\s*\("),
    # needs the GX -> GS layer
    "gx_calls": re.compile(r"\bGX[A-Z]\w*\s*\("),
}

def audit_text(text):
    text = STRIP.sub("", text)
    return Counter({k: len(r.findall(text)) for k, r in CATS.items()})

def main(root):
    root = Path(root)
    totals = {}
    for p in root.rglob("*"):
        if p.suffix not in (".c", ".cpp", ".h", ".hpp", ".inc") or not p.is_file():
            continue
        rel = p.relative_to(root).parts
        if rel[0] not in ("src", "libs", "include"):
            continue
        key = "/".join(rel[:2]) if len(rel) > 2 else rel[0]
        c = audit_text(p.read_text(encoding="utf-8", errors="replace"))
        c["files"] = 1
        c["lines"] = p.read_bytes().count(b"\n")
        totals.setdefault(key, Counter()).update(c)
    cols = ["files", "lines", *CATS]
    print(f"{'dir':28}" + "".join(f"{c[:12]:>13}" for c in cols))
    grand = Counter()
    for k in sorted(totals, key=lambda k: -totals[k]["lines"]):
        grand.update(totals[k])
        print(f"{k:28}" + "".join(f"{totals[k][c]:>13}" for c in cols))
    print(f"{'TOTAL':28}" + "".join(f"{grand[c]:>13}" for c in cols))
    gx = sorted(set().union(*(GX_NAME.findall(STRIP.sub("", p.read_text(encoding="utf-8", errors="replace")))
                              for d in ("src/Game", "src/JSystem", "src/nw4r", "libs/JSystem", "libs/nw4r")
                              for p in (root / d).rglob("*.cpp"))))
    print(f"\ndistinct GX functions used by Game/JSystem/nw4r: {len(gx)}")
    print(" ".join(gx))

def selftest():
    c = audit_text('double x = 1.0; float y = 1.0f; // 2.0\n f32 z = sqrt(x); asm { psq_l f1, 0(r3), 0, 0 } "3.0"')
    assert c["double_decl"] == 1 and c["unsuffixed_float"] == 1, c
    assert c["double_math_call"] == 1 and c["ppc_asm"] == 1 and c["paired_single"] == 1, c
    assert audit_text("GXSetTevOp(GX_TEVSTAGE0);")["gx_calls"] == 1
    print("selftest ok")

if __name__ == "__main__":
    selftest() if sys.argv[1:] == ["--selftest"] else main(sys.argv[1])
