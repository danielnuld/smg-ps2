"""What fills the stationed (resident) heaps, by system, from Dolphin MEM1/MEM2 dumps.

Stationed heaps are exp heaps (HeapMemoryWatcher.cpp:139-152), so every allocation is an 'HM' block (heapwalk.py).
Child heaps live inside a parent's block; their blocks are classified instead of the parent block, and the child's
unused room is reported as "sub-heap free". A block is classified by its first words:
- a vtable of main.dol (RMGE01) -> class name, through Petari's RMGK01 symbols. The two builds differ by small,
  shifting offsets, so the US vtables are aligned with the KR __vt__ symbols as two sequences of vtable sizes
  (difflib); each class then maps to its Petari source dir through splits.txt, or to a prefix rule.
- archive magics (RARC, Yaz0, U8), 0xDEADBABE stack fill, all zero;
- anything else is charged to the closest preceding classified object in the same heap (exp heaps hand out
  memory in order, so an object's buffers usually follow it).
ponytail: the "follows its owner" rule and the prefix rules are heuristics; good for system-sized decisions, not
for exact per-class bytes.

usage: python tools/stationed.py <dumpdir>     (dumpdir holds mem1.raw and mem2.raw; writes stationed.csv there)
"""
import bisect
import csv
import difflib
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import heapreport
from heapclass import DOL, Mem, dol_ranges
from heapwalk import chains

KR = Path(__file__).resolve().parent.parent / "vendor/Petari/config/RMGK01"
VIDEO = {693760: "XFB (YUV422 640x542)", 583712: "screen capture (BTI RGB565 640x456)", 524288: "GX FIFO"}  # content bytes
MAGIC = {b"RARC": "archive", b"Yaz0": "archive", b"\x55\xaa\x38\x2d": "archive"}
PREFIX = (  # class-name prefix -> system, for classes whose source is not split in Petari
    ("J3D", "JSystem/J3D"), ("J2D", "JSystem/J2DGraph"), ("JPA", "JSystem/JParticle"), ("JKR", "JSystem/JKernel"),
    ("JAS", "JSystem/JAudio2"), ("JAI", "JSystem/JAudio2"), ("JAU", "JSystem/JAudio2"), ("Aud", "Game/AudioLib"),
    ("Camera", "Game/Camera"), ("Layout", "Game/Screen"), ("Q34nw4r3lyt", "nw4r/lyt"), ("Q34nw4r2ut", "nw4r/ut"),
)


def us_vtables():
    """Vtables in main.dol's data sections: +4 == 0 and a run of .text pointers from +8. -> [(addr, size)]"""
    d = DOL.read_bytes()
    offs, adrs, szs = (struct.unpack(f">18I", d[k:k + 72]) for k in (0, 72, 144))
    text, data = dol_ranges()
    secs = [(o, a, s) for o, a, s in zip(offs, adrs, szs) if s]

    def u32(a):
        for o, ad, s in secs:
            if ad <= a < ad + s - 3:
                return struct.unpack_from(">I", d, o + a - ad)[0]

    intext = lambda v: v is not None and any(lo <= v < hi for lo, hi in text)
    out = []
    for lo, hi in data:
        a = lo
        while a < hi - 12:
            if u32(a + 4) == 0 and intext(u32(a + 8)) and not intext(u32(a)):
                n = 0
                while intext(u32(a + 8 + 4 * n)):
                    n += 1
                out.append((a, 8 + 4 * n))
                a += 8 + 4 * n
            else:
                a += 4
    return out


def vtable_names():
    """US vtable address -> (class, Petari source dir or '')."""
    sym = (KR / "symbols.txt").read_text()
    kr = sorted((int(a, 16), int(s, 16), n[6:]) for n, a, s in
                re.findall(r"^(__vt__\S+) = \.data:0x(\w+); // type:object size:0x(\w+)", sym, re.M))
    files = []  # (start, end, dir) of .data per split source file
    cur = None
    for line in (KR / "splits.txt").read_text().splitlines():
        if line.endswith(":") and not line.startswith(("\t", "Sections")):
            cur = line[:-1]
        m = re.match(r"\s+\.data\s+start:0x(\w+) end:0x(\w+)", line)
        if m and cur:
            files.append((int(m[1], 16), int(m[2], 16), cur.rsplit("/", 1)[0]))
    us = us_vtables()
    sm = difflib.SequenceMatcher(None, [s for _, s in us], [s for _, s, _ in kr], autojunk=False)
    out = {}
    for i, j, n in sm.get_matching_blocks():
        for k in range(n):
            ka, _, name = kr[j + k]
            src = next((f for lo, hi, f in files if lo <= ka < hi), "")
            out[us[i + k][0]] = (name, src)
    return out


def system(name, src):
    return next((s for p, s in PREFIX if re.sub(r"^\d+", "", name).startswith(p)), src or "unknown")


def classify(dumpdir):
    m1, m2 = (Path(dumpdir, f).read_bytes() for f in ("mem1.raw", "mem2.raw"))
    mem, vt = Mem(m1, m2), vtable_names()
    cs = sorted(chains([m1, m2]), key=lambda c: c["lo"])
    los = [c["lo"] for c in cs]
    big = []  # (bytes, addr, key) of the largest blocks, for checking the rules
    heaps = heapreport.report([m1, m2])
    rows = defaultdict(lambda: [0, 0])  # (system, class or kind) -> [bytes, blocks]

    def walk(head, end_owner):
        """Classify a chain's blocks in address order; returns nothing, fills rows."""
        a, blist = head, []
        while a:
            size = mem.u32(a + 4)
            blist.append((a, size + 0x10))
            a = mem.u32(a + 12) or 0
        blist.sort()
        owner = end_owner
        for a, size in blist:
            co, hi = a + 0x10, a + size
            sub = [c for c in cs[bisect.bisect_left(los, co):bisect.bisect_left(los, hi)] if c["hi"] <= hi]
            if sub:  # a child heap: classify its own top chain, its free room separately
                top = max(sub, key=lambda c: c["used"])
                walk(top["head"], owner)
                rows[("(heaps)", "sub-heap free + headers")][0] += size - top["used"]
                continue
            w0, w1 = mem.u32(co), mem.u32(co + 4)
            o = co & 0x0FFFFFFF
            raw = (m1 if co < 0x90000000 else m2)[o:o + min(size - 0x10, 0x4000)]
            if size - 0x10 in VIDEO:
                key = ("(video)", VIDEO[size - 0x10])
            elif w0 in vt:
                owner = vt[w0]
                key = (system(*owner), owner[0])
            elif raw[:4] in MAGIC:
                key = ("(archives)", MAGIC[raw[:4]])
            elif raw[:4] == b"\xde\xad\xba\xbe":
                key = ("(stacks)", "thread stack")
            elif not raw.strip(b"\0"):
                key = ("(zero)", "reserved, not written")
            elif owner:
                key = (system(*owner), owner[0] + " +data")
            else:
                key = ("unknown", "data before any object")
            rows[key][0] += size
            rows[key][1] += 1
            big.append((size, co, key))

    total = 0
    for name in ("stationed_mem1", "stationed_mem2"):
        h = heaps[name]
        top = max((c for c in cs if h["start"] <= c["lo"] and c["hi"] <= h["start"] + h["size"]),
                  key=lambda c: c["used"])
        total += top["used"]
        walk(top["head"], None)
    return rows, total, sorted(big, reverse=True)[:40]


def main(d):
    M = 2**20
    rows, total, big = classify(d)
    by_sys = defaultdict(int)
    for (s, _), (b, _) in rows.items():
        by_sys[s] += b
    out = Path(d) / "stationed.csv"
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["system", "class", "bytes", "blocks"])
        for (s, c), (b, n) in sorted(rows.items(), key=lambda kv: (-by_sys[kv[0][0]], -kv[1][0])):
            w.writerow([s, c, b, n])
    print(f"== {d}: stationed heaps used {total / M:.2f} MB (classified {sum(by_sys.values()) / M:.2f}) -> {out}")
    for s, b in sorted(by_sys.items(), key=lambda kv: -kv[1]):
        print(f"  {b / M:7.3f} MB  {s}")
    print("largest blocks:")
    for b, a, k in big:
        print(f"  {b:9}  {a:08X}  {k[0]} / {k[1]}")


if __name__ == "__main__":
    main(sys.argv[1])
