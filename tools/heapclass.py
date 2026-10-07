"""What fills the scene heaps: bytes per object class, from Dolphin MEM1/MEM2 dumps, without symbols.

Scene heaps are solid heaps (HeapMemoryWatcher.cpp:74-76): allocations sit back to back, from the head
([mStart, mSolidHead)) and from the tail ([mSolidTail, mEnd)) (JKRSolidHeap.hpp:51-54). Every NameObj starts with its
vtable pointer (+0) and its name pointer (+4) (NameObj.hpp:55). An object is accepted when +0 points into main.dol's
data sections at a vtable whose entries 2 and 3 point into .text, and +4 points at a printable Shift-JIS string.
Each object is charged everything from its start to the next object (what its constructor and init allocate right
after it lands there in a solid heap). Class = vtable address; the label is the most common instance name, and the
Petari file that writes that name as a literal when one does.
ponytail: allocations by objects that are not NameObjs, or made later for another object, are charged to whichever
NameObj precedes them; good for "which kind of thing eats the heap", not for exact per-class bytes.

usage: python tools/heapclass.py <dumpdir> [<dumpdir> ...]     (dumpdir holds mem1.raw and mem2.raw)
"""
import csv
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import heapreport

DOL = Path(__file__).resolve().parent.parent / "data/fs/sys/main.dol"
PETARI = Path(__file__).resolve().parent.parent / "vendor/Petari/src"


def dol_ranges():
    d = DOL.read_bytes()
    tadr, dadr = struct.unpack(">7I", d[72:100]), struct.unpack(">11I", d[100:144])
    tsz, dsz = struct.unpack(">7I", d[144:172]), struct.unpack(">11I", d[172:216])
    text = [(a, a + s) for a, s in zip(tadr, tsz) if s]
    data = [(a, a + s) for a, s in zip(dadr, dsz) if s]
    return text, data


class Mem:
    def __init__(self, m1, m2):
        self.m1, self.m2 = m1, m2

    def u32(self, a):
        if 0x80000000 <= a <= 0x80000000 + len(self.m1) - 4:
            return struct.unpack_from(">I", self.m1, a - 0x80000000)[0]
        if 0x90000000 <= a <= 0x90000000 + len(self.m2) - 4:
            return struct.unpack_from(">I", self.m2, a - 0x90000000)[0]
        return None

    def cstr(self, a):
        b, base = (self.m1, 0x80000000) if a < 0x90000000 else (self.m2, 0x90000000)
        o = a - base
        if not 0 <= o < len(b):
            return None
        e = b.find(b"\0", o, o + 96)
        if e <= o:
            return None
        try:
            s = b[o:e].decode("shift_jis")
        except UnicodeDecodeError:
            return None
        return s if s.isprintable() else None


def objects(mem, ranges, text, data):
    inr = lambda v, rs: v is not None and any(lo <= v < hi for lo, hi in rs)
    out = []
    for lo, hi in ranges:
        for a in range(lo, hi - 8, 4):
            w0 = mem.u32(a)
            if not inr(w0, data) or not inr(mem.u32(w0 + 8), text) or not inr(mem.u32(w0 + 12), text):
                continue
            w1 = mem.u32(a + 4)
            if w1 is None or not (0x80000000 <= w1 < 0x81800000 or 0x90000000 <= w1 < 0x94000000):
                continue
            s = mem.cstr(w1)
            if s:
                out.append((a, w0, s))
    return out


def classify(dumpdir):
    m1, m2 = (Path(dumpdir, f).read_bytes() for f in ("mem1.raw", "mem2.raw"))
    mem, (text, data) = Mem(m1, m2), dol_ranges()
    heaps = heapreport.report([m1, m2])
    per = defaultdict(lambda: [0, 0, Counter()])   # vtable -> [bytes, count, names]
    total = 0
    for name in ("scene_mem1", "scene_mem2"):
        co = heaps[name]["obj"]
        st, en, head, tail = (mem.u32(co + k) for k in (0x30, 0x34, 0x70, 0x74))
        for lo, hi in ((st, head), (tail, en)):
            objs = objects(mem, [(lo, hi)], text, data)
            total += hi - lo
            if not objs:
                per[f"(no NameObj, {name})"][0] += hi - lo
            elif objs[0][0] > lo:
                per[f"(before first object, {name})"][0] += objs[0][0] - lo
            for i, (a, vt, s) in enumerate(objs):
                nxt = objs[i + 1][0] if i + 1 < len(objs) else hi
                e = per[vt]
                e[0] += nxt - a
                e[1] += 1
                e[2][s] += 1
    return per, total


_literals = None


def petari_file(name):
    """First Petari .cpp that writes `name` as a string literal (cached index of all literals)."""
    global _literals
    if _literals is None:
        _literals = {}
        for f in PETARI.rglob("*.cpp"):
            for lit in re.findall(r'"([^"\\]{1,80})"', f.read_text(encoding="utf-8", errors="replace")):
                _literals.setdefault(lit, f.relative_to(PETARI).as_posix())
    return _literals.get(name, "")


def main(dirs):
    M = 2**20
    for d in dirs:
        per, total = classify(d)
        rows = sorted(per.items(), key=lambda kv: -kv[1][0])
        out = Path(d) / "heapclass.csv"
        with open(out, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["vtable", "bytes", "objects", "top_name", "petari_file", "names"])
            for vt, (b, n, names) in rows:
                top = names.most_common(1)[0][0] if names else ""
                w.writerow([vt if isinstance(vt, str) else f"{vt:08X}", b, n, top, petari_file(top),
                            "; ".join(f"{k}×{v}" for k, v in names.most_common(5))])
        print(f"== {d}: scene heaps used {total / M:.2f} MB, {sum(v[1] for v in per.values())} objects -> {out}")
        for vt, (b, n, names) in rows[:25]:
            top = names.most_common(1)[0][0] if names else ""
            print(f"  {b / M:6.3f} MB  {n:5}  {top[:28]:28} {petari_file(top)}")


if __name__ == "__main__":
    main(sys.argv[1:])
