"""SHP1 display lists: emitted vertices vs. Wii storage, and de-indexed PS2 size.

usage: python tools/geom.py out/sets.json     (sets.json: {label: [arc paths]})
SHP1 layout: J3DModelLoader.hpp:175-185, J3DShapeFactory.hpp:10-31,54-56; prim header GXGeometry.c:152-153.
"""
import json, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from budget import rarc_files, u16, u32

PRIMS = {0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8}           # GXEnum.h:701-709
# D (sources.md): PS2 vertex = V3-16 pos + V3-16 nrm + V4-8 color + 16-bit UV pair per tex set
PS2_ATTR = {9: 6, 10: 6, 11: 4, 12: 4, **{a: 4 for a in range(13, 21)}}

def shp1(b, o, pool=None):
    """-> (emitted vertices, ps2 bytes, display-list bytes, unknown cmds) for one SHP1 block at o.
    pool=N: geometry stays indexed in RAM and VU1 expands it (D, sources.md): each shape is cut into batches of at
    most N unique vertices; a batch = its unique vertices at PS2 stride + 1-byte index per emitted vertex + 16-byte tag.
    ponytail: a strip cut at a batch edge does not re-send its last 2 vertices; under-counts by <= 2 per batch."""
    n = u16(b, o + 8)
    init, idx, desc, dl, draw = (o + u32(b, o + k) for k in (0x0C, 0x10, 0x18, 0x20, 0x28))
    emitted = ps2 = dl_bytes = bad = 0
    for s in range(n):
        si = init + u16(b, idx + 2 * s) * 0x28
        groups, desc_off, draw_idx = u16(b, si + 2), u16(b, si + 4), u16(b, si + 8)
        stride, ps2_stride, d = 0, 0, desc + desc_off
        while u32(b, d) != 0xFF:                              # GX_VA_NULL
            attr, typ = u32(b, d), u32(b, d + 4)
            stride += 1 if attr < 9 else {0: 0, 1: 0, 2: 1, 3: 2}[typ]  # MTXIDX direct u8; INDEX8/16
            ps2_stride += PS2_ATTR.get(attr, 0) if typ else 0
            d += 8
        batch = set()
        flush = lambda: len(batch) * ps2_stride + 16 if batch else 0
        for g in range(groups):
            size, start = u32(b, draw + (draw_idx + g) * 8), u32(b, draw + (draw_idx + g) * 8 + 4)
            p, end = dl + start, dl + start + size
            dl_bytes += size
            while p < end:
                op = b[p]
                if op == 0:                                     # NOP padding
                    p += 1; continue
                if op & 0xF8 not in PRIMS:
                    bad += 1; break
                cnt = u16(b, p + 1)
                emitted += cnt
                if pool is None:
                    ps2 += cnt * ps2_stride
                else:
                    for v in range(cnt):
                        key = b[p + 3 + v * stride: p + 3 + (v + 1) * stride]
                        if key not in batch and len(batch) == pool:
                            ps2 += flush(); batch.clear()
                        batch.add(key)
                    ps2 += cnt  # u8 index per emitted vertex
                p += 3 + cnt * stride
        if pool is not None:
            ps2 += flush()
    return emitted, ps2, dl_bytes, bad

def model(b):
    r = dict(emitted=0, ps2=0, dl=0, bad=0, vtx1=0, shp1=0)
    o = 0x20
    for _ in range(u32(b, 0x0C)):
        tag, size = b[o:o + 4], u32(b, o + 4)
        if tag == b"VTX1": r["vtx1"] += size
        if tag == b"SHP1":
            r["shp1"] += size
            for k, v in zip(("emitted", "ps2", "dl", "bad"), shp1(b, o)): r[k] += v
        o += size
    return r

if __name__ == "__main__":
    for label, paths in json.load(open(sys.argv[1])).items():
        t = dict.fromkeys(("emitted", "ps2", "dl", "bad", "vtx1", "shp1"), 0)
        for p in paths:
            for name, b in rarc_files(Path(p).read_bytes()):
                if name.lower().endswith((".bmd", ".bdl")) and b[:4] == b"J3D2":
                    for k, v in model(b).items(): t[k] += v
        M = lambda x: f"{x / 2**20:.2f} MB"
        print(f"{label:12} wii VTX1+SHP1 {M(t['vtx1'] + t['shp1'])} (DL {M(t['dl'])}) | emitted {t['emitted']:,} verts"
              f" -> ps2 de-indexed {M(t['ps2'])} | bad DL cmds {t['bad']}")
