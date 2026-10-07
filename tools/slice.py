"""Phase-1 slice converter: one SMG scenario -> one little-endian blob for the PS2 engine.

usage: python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice.bin [--simplify=R]
       python tools/slice.py --selftest

Which stages and layers: budget.py's scenario logic (ScenarioData masks, StageObjInfo placement, Petari
StageDataHolder). Rows read from `jmp/placement/<layer>/{objinfo,planetobjinfo}` and `jmp/start/<layer>/startinfo`;
zones placed by `stageobjinfo` rows of the stage that holds them (pos + dir in degrees, StageDataHolder
calcPlacementMtx). Rails from `jmp/path/commonpathinfo` + `commonpathpointinfo.<n>` (control point pnt0 only).
Models: every placed planet (PlanetMapDataTable names) and the archives of the object types in OBJ_TYPES, through
geom.py (triangles per matrix group, simplified to R when R < 1). KCL copied as on disc (big-endian) from each planet
archive, converted to little-endian (kcl_to_le). ponytail: meshes are untextured indexed triangle lists with float positions; textures, UVs and strips come
when the renderer needs them (task 3.2/4.x), and only then cost bytes.

Blob (all little-endian; every payload starts 16-byte aligned, for DMA):
  header  'SMGS' u32 version=2, u32 chunk count, u32 pad          (16 B)
  chunk   tag[4], u32 payload size, 8 B pad, payload               (16 B header, payload padded to 16)
  'TYPE'  u32 n, n × char[32]                  type names; OBJS/STRT rows index this table
  'ZONE'  u32 n, n × {char[32] name, f32 pos[3], f32 rot[3]}          zone 0 = main stage (identity)
  'OBJS'  u32 n, n × ROW;  'STRT' same layout (Mario start points)
          ROW = {u16 type, u16 zone, s32 l_id, s32 path_id, f32 pos[3], f32 rot[3], f32 scale[3], s32 arg[8]}  (80 B)
  'PATH'  u32 n, n × {u16 zone, u16 closed, s32 id, u32 npts, f32 pts[npts][3]}
  'MODL'  u32 n, n × {char[32] name, u32 ngroups, ngroups × {u32 nverts, u32 ntris, f32 v[nverts][3],
          u16 idx[ntris][3], pad to 4}}
  'KCL '  u32 n, n × {char[32] model, u32 size, bytes[size] (little-endian KCL, kcl_to_le), pad to 4}
  'VUMD'  u32 n, n × {char[32] name, u32 ngroups, ngroups × {u32 nverts (3 per triangle), pad to 16,
          f32 pos[nverts][4] (w = 1), u32 rgba[nverts][4]}}: the same triangles de-indexed for the VU1 renderer,
          flat shaded per triangle with a fixed light baked in (BASE_COLOUR per model). ponytail: 32 B per emitted
          vertex; the compact indexed form (budget.py --vu-index) is the next step once the VU1 path runs.
  'GRAV'  u32 n, n × {u16 kind (0 point, 1 plane), u16 zone, s32 priority, s32 inverse, f32 range, f32 distant,
          f32 pos[3], f32 up[3]}   (Petari GravityCreator/GravityUtil: point distant = 500 * scale.x, plane up = rotated
          Y axis; Range/Distant/Priority/Inverse from the placement row when present, defaults -1, 0, 0, 0)
"""
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B
import geom

OBJ_TYPES = ("Coin", "PurpleCoin", "StarPiece", "Kuribo", "CirclePurpleCoinGroup", "PurpleRailCoin")
ROW_FIELDS = ("pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z")
ROW = struct.Struct("<HHii9f8i")


def col(b, field, n, default=0):
    v = B.bcsv_column(b, field)
    return v if len(v) == n else [default] * n


def float_column(b, field):
    """BCSV float (type 2) column: bcsv_column only decodes int/string types, so read floats here (same header
    layout, JMapInfo.hpp:6-31)."""
    if len(b) < 16:
        return []
    n, nf, data, esize = struct.unpack_from(">4I", b, 0)
    want = B.jmap_hash(field)
    for i in range(nf):
        fh, mask, foff, shift, ftype = struct.unpack_from(">IIHBB", b, 16 + i * 12)
        if fh == want:
            if ftype != 2:
                return []
            return [struct.unpack_from(">f", b, data + e * esize + foff)[0] for e in range(n)]
    return []


def int_or(b, field, n, default):
    v = B.bcsv_column(b, field)
    v = [x - 2**32 if isinstance(x, int) and x >= 2**31 else x for x in v]
    return v if len(v) == n else [default] * n


def rows(b):
    names = B.bcsv_column(b, "name")
    n = len(names)
    if not n:
        return []
    fl = {f: (float_column(b, f) or [0.0] * n) for f in ROW_FIELDS}
    args = [col(b, f"Obj_arg{i}", n, -1) for i in range(8)]
    lid = col(b, "l_id", n, -1)
    path = [v - 0x10000 if 0x8000 <= v < 0x10000 else v for v in col(b, "CommonPath_ID", n, -1)]  # SHORT, unsigned in bcsv_column
    out = []
    for i in range(n):
        out.append(dict(name=names[i], l_id=lid[i], path=path[i], pos=[fl[f][i] for f in ROW_FIELDS[:3]],
                        rot=[fl[f][i] for f in ROW_FIELDS[3:6]], scale=[fl[f][i] or 1.0 for f in ROW_FIELDS[6:]],
                        args=[a[i] for a in args], range=(float_column(b, "Range") or [-1.0] * n)[i],
                        distant=(float_column(b, "Distant") or [None] * n)[i],
                        priority=int_or(b, "Priority", n, 0)[i], inverse=int_or(b, "Inverse", n, 0)[i]))
    return out


def stage_files(path):
    return {p.lower(): d for p, d in B.rarc_files(path.read_bytes(), paths=True)}


def scene(fs, galaxy, scenario):
    fs = Path(fs)
    st = fs / "StageData"
    cache = {}
    sd = {n.lower(): d for n, d in B.rarc_files((st / galaxy / f"{galaxy}Scenario.arc").read_bytes())}["scenariodata.bcsv"]
    up = {}
    act = B.active_stages(galaxy, B.scenario_masks(sd)[scenario],
                          lambda s: B.arc_layers(st / f"{s}.arc", cache)["layers"] if (st / f"{s}.arc").exists() else {}, up)
    zones, objs, starts, paths = [dict(name=galaxy, pos=[0, 0, 0], rot=[0, 0, 0])], [], [], []
    zone_id = {galaxy: 0}
    order = [galaxy] + [s for s in act if s != galaxy]
    for s in order:
        files = stage_files(st / f"{s}.arc")
        root = next(iter(files)).split("/")[0]
        for layer in act[s]:
            for z in rows(files.get(f"{root}/jmp/placement/{layer}/stageobjinfo", b"")):
                if z["name"] in act and z["name"] not in zone_id:
                    zone_id[z["name"]] = len(zones)
                    zones.append(dict(name=z["name"], pos=z["pos"], rot=z["rot"]))
        zid = zone_id.get(s, 0)
        for layer in act[s]:
            for f in ("objinfo", "planetobjinfo"):
                for r in rows(files.get(f"{root}/jmp/placement/{layer}/{f}", b"")):
                    objs.append(dict(r, zone=zid))
            for r in rows(files.get(f"{root}/jmp/start/{layer}/startinfo", b"")):
                starts.append(dict(r, zone=zid))
        info = files.get(f"{root}/jmp/path/commonpathinfo", b"")
        ids, closed = col(info, "l_id", len(B.bcsv_column(info, "name")), -1), B.bcsv_column(info, "closed")
        for i, pid in enumerate(ids):
            pts = files.get(f"{root}/jmp/path/commonpathpointinfo.{i}", b"")
            xs, ys, zs = (float_column(pts, f"pnt0_{a}") for a in "xyz")
            paths.append(dict(zone=zid, id=pid, closed=int(str(closed[i] if i < len(closed) else "").lower() == "close"),
                              pts=list(zip(xs, ys, zs))))
    return act, zones, objs, starts, paths


def model_groups(arc, ratio):
    """-> (groups [(verts, tris)], kcl bytes or None) for the first J3D model of an archive."""
    groups, kcl = [], None
    for name, b in B.rarc_files(arc.read_bytes()):
        n = name.lower()
        if n.endswith((".bdl", ".bmd")) and b[:4] == b"J3D2" and not groups:
            got = []
            geom.model(b, ratio, None, got)
            for pos, tri in got:
                if tri:
                    used = sorted(set(tri))
                    remap = {v: i for i, v in enumerate(used)}
                    verts = [pos[3 * v:3 * v + 3] for v in used]
                    groups.append((verts, [remap[v] for v in tri]))
        elif n.endswith(".kcl") and kcl is None:
            kcl = b
    return groups, kcl


def kcl_to_le(b):
    """KCL big-endian -> little-endian, same layout (Petari KCollision.hpp:9-41, KCollision.cpp searchBlock):
    header 14 words; pos and normal arrays (f32); prisms from prismOffset+0x10 (1-based), 0x10 each {f32 height,
    u16 pos, nrm, edge0..2, attribute}; octree: a root grid of s32, each >= 0 = offset (from the current node) to 8
    child s32, < 0 = (value & 0x7FFFFFFF) offset to a u16 prism list whose entries after the first run until 0."""
    out = bytearray(b)
    sw32 = lambda o: out.__setitem__(slice(o, o + 4), b[o:o + 4][::-1])
    sw16 = lambda o: out.__setitem__(slice(o, o + 2), b[o:o + 2][::-1])
    pos, nrm, prism, octree = struct.unpack_from(">4I", b, 0)
    xmask, ymask, zmask, shift, xshift, xyshift = struct.unpack_from(">6i", b, 0x20)
    for o in range(0, 0x38, 4):
        sw32(o)
    for o in range(pos, prism + 0x10, 4):   # positions then normals, all f32
        sw32(o)
    for o in range(prism + 0x10, octree, 0x10):
        sw32(o)
        for k in range(6):
            sw16(o + 4 + 2 * k)
    blocks = lambda m: ((~m & 0xFFFFFFFF) >> shift) + 1   # root cells along an axis (mask = ~(extent - 1))
    root = 1 if xshift == -1 and xyshift == -1 else blocks(xmask) * blocks(ymask) * blocks(zmask)
    seen_nodes, seen_lists = set(), set()
    todo = [(octree, root)]
    while todo:
        base, count = todo.pop()
        if base in seen_nodes:
            continue
        seen_nodes.add(base)
        for i in range(count):
            e = base + 4 * i
            v = struct.unpack_from(">i", b, e)[0]
            sw32(e)
            if v >= 0:
                todo.append((base + v, 8))
            else:
                lst = base + (v & 0x7FFFFFFF)
                if lst in seen_lists:
                    continue
                seen_lists.add(lst)
                o = lst
                sw16(o)
                o += 2
                while True:
                    sw16(o)
                    if struct.unpack_from(">H", b, o)[0] == 0:
                        break
                    o += 2
    return bytes(out)


BASE_COLOUR = {"Coin": (240, 208, 48), "PurpleCoin": (176, 80, 240), "StarPiece": (96, 208, 240),
               "Kuribo": (144, 80, 32)}
LIGHT = (0.35, 0.75, -0.55)


def vu_groups(groups, base):
    """Indexed groups -> [(pos qwords, colour qwords)] per group, flat shading: k = 0.25 + 0.75 |n . L|."""
    ll = math.sqrt(sum(c * c for c in LIGHT))
    L = [c / ll for c in LIGHT]
    out = []
    for verts, tris in groups:
        pos, col = bytearray(), bytearray()
        for t in range(0, len(tris), 3):
            a, b, c = (verts[tris[t + k]] for k in range(3))
            e1 = [b[i] - a[i] for i in range(3)]
            e2 = [c[i] - a[i] for i in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            nl = math.sqrt(sum(x * x for x in n)) or 1.0
            k = 0.25 + 0.75 * abs(sum(n[i] * L[i] for i in range(3))) / nl
            rgba = struct.pack("<4I", int(base[0] * k), int(base[1] * k), int(base[2] * k), 0x80)
            for v in (a, b, c):
                pos += struct.pack("<4f", v[0], v[1], v[2], 1.0)
                col += rgba
        out.append((bytes(pos), bytes(col)))
    return out


def chunk(tag, payload):
    payload += b"\0" * (-len(payload) % 16)
    return tag + struct.pack("<I8x", len(payload)) + payload


def name32(s):
    return s.encode("ascii", "replace")[:31].ljust(32, b"\0")


def build(fs, galaxy, scenario, ratio=0.99):
    act, zones, objs, starts, paths = scene(fs, galaxy, scenario)
    fsp = Path(fs)
    planets = B.planet_table(fsp, lambda a: (fsp / "ObjectData" / f"{a}.arc").exists())
    keep = [o for o in objs if o["name"] in OBJ_TYPES or o["name"] in planets or "Gravity" in o["name"]]
    types = sorted({o["name"] for o in keep} | {s["name"] for s in starts})
    tid = {t: i for i, t in enumerate(types)}
    pack_row = lambda r: ROW.pack(tid[r["name"]], r["zone"], r["l_id"], r["path"], *r["pos"], *r["rot"], *r["scale"],
                                  *[a if -2**31 <= a < 2**31 else -1 for a in r["args"]])
    out = [chunk(b"TYPE", struct.pack("<I", len(types)) + b"".join(name32(t) for t in types)),
           chunk(b"ZONE", struct.pack("<I", len(zones)) + b"".join(
               name32(z["name"]) + struct.pack("<6f", *z["pos"], *z["rot"]) for z in zones)),
           chunk(b"OBJS", struct.pack("<I", len(keep)) + b"".join(pack_row(r) for r in keep)),
           chunk(b"STRT", struct.pack("<I", len(starts)) + b"".join(pack_row(r) for r in starts)),
           chunk(b"PATH", struct.pack("<I", len(paths)) + b"".join(
               struct.pack("<HHiI", p["zone"], p["closed"], p["id"], len(p["pts"])) +
               b"".join(struct.pack("<3f", *pt) for pt in p["pts"]) for p in paths))]
    grav = []
    for o in keep:
        kind = {"GlobalPointGravity": 0, "GlobalPlaneGravity": 1}.get(o["name"])
        if kind is None:
            continue
        d = math.radians
        rx, ry, rz = (d(a) for a in o["rot"])
        # rotated Y axis of R = Rz * Ry * Rx (same order as the engine's placement matrices)
        up = [math.cos(rz) * math.sin(ry) * math.sin(rx) - math.sin(rz) * math.cos(rx),
              math.sin(rz) * math.sin(ry) * math.sin(rx) + math.cos(rz) * math.cos(rx), math.cos(ry) * math.sin(rx)]
        distant = o["distant"] if o["distant"] is not None else (500.0 * o["scale"][0] if kind == 0 else 0.0)
        grav.append(struct.pack("<HHiiff3f3f", kind, o["zone"], o["priority"], 1 if o["inverse"] == 1 else 0,
                                o["range"], distant, *o["pos"], *up))
    out.append(chunk(b"GRAV", struct.pack("<I", len(grav)) + b"".join(grav)))
    models, kcls, vumd = [], [], []
    for t in [t for t in types if t in planets] + [t for t in types if t in OBJ_TYPES]:
        arc = fsp / "ObjectData" / f"{t}.arc"
        if not arc.exists():
            continue
        groups, kcl = model_groups(arc, ratio)
        g = b""
        for verts, tris in groups:
            idx = struct.pack(f"<{len(tris)}H", *tris)
            g += struct.pack("<II", len(verts), len(tris) // 3) + b"".join(struct.pack("<3f", *v) for v in verts)
            g += idx + b"\0" * (-len(idx) % 4)
        models.append(name32(t) + struct.pack("<I", len(groups)) + g)
        vg = b""
        for pos, col in vu_groups(groups, BASE_COLOUR.get(t, (112, 176, 96))):
            vg += struct.pack("<I12x", len(pos) // 16) + pos + col
        vumd.append(name32(t) + struct.pack("<I12x", len(groups)) + vg)
        if kcl and t in planets:
            kcl = kcl_to_le(kcl)
            kcls.append(name32(t) + struct.pack("<I", len(kcl)) + kcl + b"\0" * (-len(kcl) % 4))
    out.append(chunk(b"MODL", struct.pack("<I", len(models)) + b"".join(models)))
    out.append(chunk(b"VUMD", struct.pack("<I12x", len(vumd)) + b"".join(vumd)))
    out.append(chunk(b"KCL ", struct.pack("<I", len(kcls)) + b"".join(kcls)))
    blob = b"SMGS" + struct.pack("<II4x", 2, len(out)) + b"".join(out)
    blob += b"\0" * (-len(blob) % 16)
    return blob, dict(stages=act, types=types, objs=len(keep), starts=len(starts), paths=len(paths),
                      models=len(models), kcls=len(kcls))


def read_chunks(blob):
    """Parse a blob back: -> {tag: payload}. Used by the selftest and handy for checking files."""
    assert blob[:4] == b"SMGS", blob[:4]
    n = struct.unpack_from("<I", blob, 8)[0]
    o, out = 16, {}
    for _ in range(n):
        tag, size = blob[o:o + 4], struct.unpack_from("<I", blob, o + 4)[0]
        out[tag] = blob[o + 16:o + 16 + size]
        o += 16 + size
    return out


def selftest():
    # BCSV with name (string) + pos_x (float) + Obj_arg0 (long): 2 rows
    fields = [("name", 0, 6), ("pos_x", 4, 2), ("Obj_arg0", 8, 0)]
    hdr = struct.pack(">4I", 2, 3, 16 + 36, 12) + b"".join(
        struct.pack(">IIHBB", B.jmap_hash(f), 0xFFFFFFFF, o, 0, t) for f, o, t in fields)
    strings = b"Coin\0Kuribo\0"
    data = struct.pack(">If i", 0, 1.5, 7) + struct.pack(">Ifi", 5, -2.0, -1)
    b = hdr + data + strings
    assert float_column(b, "pos_x") == [1.5, -2.0]
    r = rows(b)
    assert [x["name"] for x in r] == ["Coin", "Kuribo"] and r[0]["args"][0] == 7 and r[1]["pos"][0] == -2.0, r
    assert r[0]["scale"] == [1.0, 1.0, 1.0] and r[0]["path"] == -1
    # round trip of the chunk framing
    blob = b"SMGS" + struct.pack("<II4x", 2, 2) + chunk(b"TYPE", struct.pack("<I", 1) + name32("Coin")) + chunk(b"KCL ", b"x")
    c = read_chunks(blob)
    assert c[b"TYPE"][4:8] == b"Coin" and c[b"KCL "][:1] == b"x", c
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        ratio = next((float(a.split("=")[1]) for a in sys.argv if a.startswith("--simplify=")), 0.99)
        args = [a for a in sys.argv[1:] if not a.startswith("--")]
        blob, info = build(args[0], args[1], int(args[2]), ratio)
        Path(args[3]).parent.mkdir(parents=True, exist_ok=True)
        Path(args[3]).write_bytes(blob)
        print(info)
        chunks = read_chunks(blob)
        k = chunks.get(b"KCL ", b"")
        if len(k) > 44:  # little-endian check: the first header word is the pos offset 0x38
            assert struct.unpack_from("<I", k, 40)[0] == 0x38, "KCL in the blob is not little-endian"
        sizes = {k.decode(): len(v) for k, v in chunks.items()}
        print(f"blob {len(blob)} B ({len(blob) / 2**20:.2f} MB), simplify R = {ratio}, chunks {sizes}")
