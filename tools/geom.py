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
COMP = {0: "B", 1: "b", 2: "H", 3: "h", 4: "f"}                  # GXEnum.h:898-900 comp types u8 s8 u16 s16 f32

VTX1_ARRAY = {9: 0x0C, 10: 0x10, 11: 0x18, 12: 0x1C, **{13 + k: 0x20 + 4 * k for k in range(8)}}  # J3DModelLoader.hpp:70-77


def vtx1_attr(b, v, attr=9):
    """VTX1 block at v -> index -> floats of one attribute (POS = 9 xyz, TEX0.. = 13.. st). J3DModelLoader.hpp:70-77
    (fmt list @+8, arrays from @+0x0C); fmt entry {attr, cnt, type, frac} (GXStruct.h:32-37) padded to 16 bytes;
    POS cnt 0 = XY, 1 = XYZ; TEX cnt 0 = S, 1 = ST (GXEnum.h:387-392)."""
    f = v + u32(b, v + 8)
    while u32(b, f) != 0xFF:
        if u32(b, f) == attr:
            cnt, typ, frac = u32(b, f + 4), u32(b, f + 8), b[f + 12]
            n, want = (cnt + 2, 3) if attr == 9 else (cnt + 1, 2)
            c = COMP[typ]
            sz, scale, base = struct.calcsize(c), (1.0 if typ == 4 else 1.0 / (1 << frac)), v + u32(b, v + VTX1_ARRAY[attr])
            def get(i):
                xs = [x * scale for x in struct.unpack_from(">" + c * n, b, base + i * n * sz)]
                return xs + [0.0] * (want - n)
            return get
        f += 16
    return None


vtx1_positions = vtx1_attr


def triangles(op, v):
    """GX primitive (GXEnum.h:701-709) -> flat triangle list, degenerate triangles dropped; None if not triangles."""
    op &= 0xF8
    if op == 0x90:
        t = v[:len(v) // 3 * 3]
    elif op == 0x98:
        t = [x for i in range(len(v) - 2) for x in ((v[i], v[i + 1], v[i + 2]) if i % 2 == 0 else (v[i + 1], v[i], v[i + 2]))]
    elif op == 0xA0:
        t = [x for i in range(1, len(v) - 1) for x in (v[0], v[i], v[i + 1])]
    elif op == 0x80:
        t = [x for q in range(0, len(v) - 3, 4) for x in (v[q], v[q + 1], v[q + 2], v[q], v[q + 2], v[q + 3])]
    else:
        return None
    return [x for i in range(0, len(t), 3) if len({t[i], t[i + 1], t[i + 2]}) == 3 for x in t[i:i + 3]]

def shp1(b, o, pool=None, ratio=1.0, positions=None, collect=None):
    """-> (emitted vertices, ps2 bytes, display-list bytes, unknown cmds) for one SHP1 block at o.
    pool=N: geometry stays indexed in RAM and VU1 expands it (D, sources.md): each shape is cut into batches of at
    most N unique vertices; a batch = its unique vertices at PS2 stride + 1-byte index per emitted vertex + 16-byte tag.
    ponytail: a strip cut at a batch edge does not re-send its last 2 vertices; under-counts by <= 2 per batch.
    ratio<1 (needs positions from vtx1_positions): each matrix group's triangles are simplified to ~ratio of their count
    (meshoptimizer, tools/simplify.py), re-stripped with degenerate joins, then sized the same way; vertices never merge
    across groups (each group has its own matrix table). Lines/points are kept as they are.
    collect: list that receives (flat positions, triangle list, info) per group, after simplification; info = {shape,
    at: attr -> (offset, width) of its index in a vertex key, keys: vertex key bytes per vertex id}."""
    n = u16(b, o + 8)
    init, idx, desc, dl, draw = (o + u32(b, o + k) for k in (0x0C, 0x10, 0x18, 0x20, 0x28))
    emitted = ps2 = dl_bytes = bad = 0
    for s in range(n):
        si = init + u16(b, idx + 2 * s) * 0x28
        groups, desc_off, draw_idx = u16(b, si + 2), u16(b, si + 4), u16(b, si + 8)
        stride, ps2_stride, d = 0, 0, desc + desc_off
        at = {}                                               # attr -> (byte offset, width) of its index in a vertex
        while u32(b, d) != 0xFF:                              # GX_VA_NULL
            attr, typ = u32(b, d), u32(b, d + 4)
            if typ in (2, 3):
                at[attr] = (stride, typ - 1)
            stride += 1 if attr < 9 else {0: 0, 1: 0, 2: 1, 3: 2}[typ]  # MTXIDX direct u8; INDEX8/16
            ps2_stride += PS2_ATTR.get(attr, 0) if typ else 0
            d += 8
        pos_at = at.get(9)
        batch = set()
        flush = lambda: len(batch) * ps2_stride + 16 if batch else 0
        simp = (ratio < 1 or collect is not None) and positions is not None and pos_at is not None
        def emit(keys):
            nonlocal emitted, ps2
            emitted += len(keys)
            if pool is None:
                ps2 += len(keys) * ps2_stride
                return
            for key in keys:
                if key not in batch and len(batch) == pool:
                    ps2 += flush(); batch.clear()
                batch.add(key)
            ps2 += len(keys)  # u8 index per emitted vertex
        for g in range(groups):
            size, start = u32(b, draw + (draw_idx + g) * 8), u32(b, draw + (draw_idx + g) * 8 + 4)
            p, end = dl + start, dl + start + size
            dl_bytes += size
            ids, pos, tri = {}, [], []
            while p < end:
                op = b[p]
                if op == 0:                                     # NOP padding
                    p += 1; continue
                if op & 0xF8 not in PRIMS:
                    bad += 1; break
                cnt = u16(b, p + 1)
                keys = [b[p + 3 + v * stride: p + 3 + (v + 1) * stride] for v in range(cnt)]
                t = None
                if simp:
                    vids = []
                    for k in keys:
                        if k not in ids:
                            ids[k] = len(ids)
                            off, w = pos_at
                            pos += positions(k[off] if w == 1 else (k[off] << 8 | k[off + 1]))
                        vids.append(ids[k])
                    t = triangles(op, vids)
                if t is None or ratio >= 1:
                    emit(keys)
                if t is not None:
                    tri += t
                p += 3 + cnt * stride
            if tri:
                from simplify import simplify, stripify
                new = simplify(tri, pos, ratio)[0] if ratio < 1 else tri
                if ratio < 1:
                    emit([(g, i) for i in stripify(new, len(ids))])
                if collect is not None:
                    keys_of = sorted(ids, key=ids.get)
                    collect.append((pos, new, dict(shape=s, at=at, keys=keys_of)))
        if pool is not None:
            ps2 += flush()
    return emitted, ps2, dl_bytes, bad

def model(b, ratio=1.0, pool=None, collect=None):
    r = dict(emitted=0, ps2=0, dl=0, bad=0, vtx1=0, shp1=0)
    o, pos = 0x20, None
    for _ in range(u32(b, 0x0C)):
        tag, size = b[o:o + 4], u32(b, o + 4)
        if tag == b"VTX1":
            r["vtx1"] += size
            pos = vtx1_positions(b, o)
        if tag == b"SHP1":
            r["shp1"] += size
            for k, v in zip(("emitted", "ps2", "dl", "bad"), shp1(b, o, pool, ratio, pos, collect)): r[k] += v
        o += size
    return r

def selftest():
    assert triangles(0x98, [0, 1, 2, 3]) == [0, 1, 2, 2, 1, 3]
    assert triangles(0xA0, [0, 1, 2, 3]) == [0, 1, 2, 0, 2, 3]
    assert triangles(0x80, [0, 1, 2, 3]) == [0, 1, 2, 0, 2, 3]
    assert triangles(0x90, [0, 0, 1, 2, 3, 4]) == [2, 3, 4] and triangles(0xA8, [0, 1]) is None
    # J3D-like buffer: VTX1 with a flat 21x21 s16 grid (frac 4), SHP1 with one triangle-list prim over it
    g, n_tri = 21, 800
    vtx = bytearray(0x40)
    struct.pack_into(">4sI", vtx, 0, b"VTX1", 0)
    struct.pack_into(">II", vtx, 8, 0x20, 0x40)                      # fmt list @+0x20, pos @+0x40
    vtx[0x20:0x40] = struct.pack(">IIIB3x", 9, 1, 3, 4) + struct.pack(">I12x", 0xFF)
    vtx += b"".join(struct.pack(">3h", x * 16, y * 16, 0) for y in range(g) for x in range(g))
    pos = vtx1_positions(bytes(vtx), 0)
    assert pos(g + 2) == [2.0, 1.0, 0.0], pos(g + 2)
    idx = [a for y in range(g - 1) for x in range(g - 1) for a in
           (y * g + x, y * g + x + 1, y * g + x + g, y * g + x + 1, y * g + x + g + 1, y * g + x + g)]
    shp = bytearray(0xC0)
    struct.pack_into(">H", shp, 8, 1)
    struct.pack_into(">5I", shp, 0x0C, 0x40, 0x68, 0, 0x70, 0)
    struct.pack_into(">I", shp, 0x20, 0xC0); struct.pack_into(">I", shp, 0x28, 0xB0)
    struct.pack_into(">HHHH", shp, 0x40, 0, 1, 0, 0)
    struct.pack_into(">4I", shp, 0x70, 9, 3, 0xFF, 0)
    dl = bytes([0x90]) + struct.pack(">H", len(idx)) + struct.pack(f">{len(idx)}H", *idx)
    struct.pack_into(">II", shp, 0xB0, len(dl), 0)
    shp = bytes(shp) + dl
    full = shp1(shp, 0)
    half = shp1(shp, 0, None, 0.5, pos)
    assert full[0] == 3 * n_tri and half[0] < full[0] // 3, (full, half)   # list -> strip, and fewer triangles
    assert shp1(shp, 0, None, 1.0, pos) == full                            # ratio 1 is the untouched path
    got = []
    assert shp1(shp, 0, None, 1.0, pos, got) == full and len(got[0][1]) == 3 * n_tri  # collecting changes no size
    print("selftest ok", full[0], "->", half[0], "emitted vertices at R=0.5")

if __name__ == "__main__" and sys.argv[1:] == ["--selftest"]:
    selftest()
elif __name__ == "__main__":
    for label, paths in json.load(open(sys.argv[1])).items():
        t = dict.fromkeys(("emitted", "ps2", "dl", "bad", "vtx1", "shp1"), 0)
        for p in paths:
            for name, b in rarc_files(Path(p).read_bytes()):
                if name.lower().endswith((".bmd", ".bdl")) and b[:4] == b"J3D2":
                    for k, v in model(b).items(): t[k] += v
        M = lambda x: f"{x / 2**20:.2f} MB"
        print(f"{label:12} wii VTX1+SHP1 {M(t['vtx1'] + t['shp1'])} (DL {M(t['dl'])}) | emitted {t['emitted']:,} verts"
              f" -> ps2 de-indexed {M(t['ps2'])} | bad DL cmds {t['bad']}")
