"""Sum JKRExpHeap used blocks in Dolphin MEM1/MEM2 dumps (no symbols needed, any region).

Used block (JKRExpHeap.hpp CMemBlock, JKRExpHeap.cpp appendUsedList):
  0x00 u16 magic 'HM', 0x02 u8 flags, 0x03 u8 group, 0x04 u32 size (content), 0x08 prev, 0x0C next; content at +0x10.
Each heap keeps its used blocks in one prev/next list, so a chain = one heap's used set. A block counts only if its
links agree with its neighbours (stale 'HM' in freed memory is dropped). Child heaps live inside a parent's used
block, so their bytes appear in both: the report marks nested chains.

usage: python tools/heapwalk.py mem1.raw mem2.raw     (Dolphin: Memory view -> Dump MEM1 / Dump MEM2)
       python tools/heapwalk.py --selftest
"""
import struct
import sys

MAGIC = 0x484D  # 'HM'
REGIONS = ((0x80000000, 0x01800000), (0x90000000, 0x04000000))  # MEM1 24 MB, MEM2 64 MB (cached mirrors)


class Mem:
    def __init__(self, dumps):
        self.parts = [(base, data) for (base, _), data in zip(REGIONS, dumps)]

    def u32(self, addr):
        for base, data in self.parts:
            if base <= addr <= base + len(data) - 4:
                return struct.unpack_from(">I", data, addr - base)[0]
        return None


def chains(dumps):
    mem = Mem(dumps)
    blocks = {}  # addr -> (size, prev, next)
    for base, data in mem.parts:
        off = data.find(b"HM")
        while 0 <= off <= len(data) - 16:
            if off % 4 == 0:
                size, prev, nxt = struct.unpack_from(">III", data, off + 4)
                blocks[base + off] = (size, prev, nxt)
            off = data.find(b"HM", off + 1)
    ok = lambda a, b: b == 0 or (b in blocks and blocks[b][1] == a)  # next's prev points back
    good = {a for a, (s, p, n) in blocks.items() if ok(a, n) and (p == 0 or (p in blocks and blocks[p][2] == a))}
    heads = [a for a in good if blocks[a][1] == 0 or blocks[a][1] not in good]
    out = []
    for h in heads:
        a, n, used, lo, hi = h, 0, 0, h, h
        seen = set()
        while a in good and a not in seen:
            seen.add(a)
            size = blocks[a][0]
            n += 1; used += size + 0x10
            lo, hi = min(lo, a), max(hi, a + 0x10 + size)
            a = blocks[a][2]
        out.append(dict(head=h, blocks=n, used=used, lo=lo, hi=hi))
    out.sort(key=lambda c: -c["used"])
    for c in out:  # nested: whole span inside one used block of another chain
        c["inside"] = next((f"{o['head']:08X}" for o in out if o is not c and o["lo"] <= c["lo"] and c["hi"] <= o["hi"]
                            and o["used"] > c["used"]), "")
    return out


def heaps(dumps, cs=None):
    """JKRHeap objects found at the start of a used block's content (JKRHeap.hpp: mStart 0x30, mEnd 0x34,
    mSize 0x38). Solid heaps (JKRSolidHeap.hpp: mFreeSize 0x6C, mSolidHead 0x70, mSolidTail 0x74) report
    used = mSize - mFreeSize; others report the largest used-block chain inside [mStart, mEnd)."""
    mem = Mem(dumps)
    cs = cs if cs is not None else chains(dumps)
    out = []
    for c in cs:
        a = c["head"]
        while a:
            size, nxt = mem.u32(a + 4), mem.u32(a + 12)
            co = a + 0x10
            st, en, sz = (mem.u32(co + k) for k in (0x30, 0x34, 0x38))
            if None not in (st, en, sz, size) and co < st <= en <= co + size and sz == en - st and sz >= 0x1000:
                free, head, tail = (mem.u32(co + k) for k in (0x6C, 0x70, 0x74))
                if None not in (free, head, tail) and st <= head <= tail <= en and free == tail - head:
                    out.append(dict(obj=co, start=st, size=sz, kind="solid", used=sz - free))
                else:
                    inner = [x["used"] for x in cs if st <= x["lo"] and x["hi"] <= en]
                    out.append(dict(obj=co, start=st, size=sz, kind="exp", used=max(inner, default=0)))
            a = nxt if nxt and mem.u32(nxt) is not None else 0
    out.sort(key=lambda h: -h["size"])
    return out


def report(cs, min_bytes=0x10000):
    print(f"{'head':>8} {'span':>19} {'blocks':>7} {'used MB':>8}  inside")
    for c in cs:
        if c["used"] >= min_bytes:
            print(f"{c['head']:08X} {c['lo']:08X}-{c['hi']:08X} {c['blocks']:7} {c['used'] / 2**20:8.2f}  {c['inside']}")
    top = sum(c["used"] for c in cs if not c["inside"])
    print(f"top-level used: {top / 2**20:.2f} MB (nested chains excluded)")


def selftest():
    mem1 = bytearray(0x1000)
    put = lambda off, size, prev, nxt: struct.pack_into(">HBBIII", mem1, off, MAGIC, 0, 0, size, prev, nxt)
    # heap A: 3 blocks at 0x100, 0x200, 0x400; heap B nested inside A's 0x400 block; stale 'HM' at 0x800
    put(0x100, 0xF0, 0, 0x80000200)
    put(0x200, 0x1F0, 0x80000100, 0x80000400)
    put(0x400, 0x300, 0x80000200, 0)
    put(0x420, 0x40, 0, 0x80000480)
    put(0x480, 0x40, 0x80000420, 0)
    put(0x800, 0x10, 0x80000300, 0x80000900)  # links do not agree -> dropped
    cs = chains([bytes(mem1), b""])
    assert [(c["head"], c["blocks"], c["used"]) for c in cs] == [
        (0x80000100, 3, 0xF0 + 0x1F0 + 0x300 + 0x30), (0x80000420, 2, 0x40 * 2 + 0x20)], cs
    assert cs[1]["inside"] == "80000100" and cs[0]["inside"] == "", cs
    # solid heap object in a block at 0xA00 (content 0xA10): mStart 0xA90, mEnd 0x2A90, head/tail -> 0x600 used
    m = bytearray(0x3000)
    struct.pack_into(">HBBIII", m, 0xA00, MAGIC, 0, 0, 0x2080, 0, 0)
    struct.pack_into(">III", m, 0xA10 + 0x30, 0x80000A90, 0x80002A90, 0x2000)
    struct.pack_into(">III", m, 0xA10 + 0x6C, 0x1A00, 0x80001090, 0x80002A90)
    hs = heaps([bytes(m), b""])
    assert [(h["kind"], h["size"], h["used"]) for h in hs] == [("solid", 0x2000, 0x600)], hs
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        dumps = [open(p, "rb").read() for p in sys.argv[1:3]]
        cs = chains(dumps)
        report(cs)
        print()
        print(f"{'heap obj':>8} {'kind':>5} {'size MB':>8} {'used MB':>8}")
        for h in heaps(dumps, cs):
            if h["size"] >= 0x10000:
                print(f"{h['obj']:08X} {h['kind']:>5} {h['size'] / 2**20:8.2f} {h['used'] / 2**20:8.2f}")
