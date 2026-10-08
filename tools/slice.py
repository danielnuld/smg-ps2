"""Phase-1 slice converter: one SMG scenario -> one little-endian blob for the PS2 engine.

usage: python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice.bin [--simplify=R]
       python tools/slice.py --selftest

Which stages and layers: budget.py's scenario logic (ScenarioData masks, StageObjInfo placement, Petari
StageDataHolder). Rows read from `jmp/placement/<layer>/{objinfo,planetobjinfo}` and `jmp/start/<layer>/startinfo`;
zones placed by `stageobjinfo` rows of the stage that holds them (pos + dir in degrees, StageDataHolder
calcPlacementMtx). Rails from `jmp/path/commonpathinfo` + `commonpathpointinfo.<n>` (control point pnt0 only).
Models: every placed planet (PlanetMapDataTable names) and the archives of the object types in OBJ_TYPES, through
geom.py (triangles per matrix group, simplified to R when R < 1). KCL copied as on disc (big-endian) from each planet
archive, converted to little-endian (kcl_to_le). Streaming layout of a scenario: packs() (task 5.1).

Blob (all little-endian; every payload starts 16-byte aligned, for DMA):
  header  'SMGS' u32 version=5, u32 chunk count, u32 pad          (16 B)
  chunk   tag[4], u32 payload size, 8 B pad, payload               (16 B header, payload padded to 16)
  'TYPE'  u32 n, n × char[32]                  type names; OBJS/STRT rows index this table
  'ZONE'  u32 n, n × {char[32] name, f32 pos[3], f32 rot[3]}   world placement; zone 0 = main stage (identity).
          Everything else placed in a zone is already in world space (to_world); CAMS stays zone-local
  'ZPAR'  u32 n, n × s32 parent zone (-1 for the main stage): the zone chain for streaming
  'OBJS'  u32 n, n × ROW;  'STRT' same layout (Mario start points)
          ROW = {u16 type, u16 zone, s32 l_id, s32 path_id, f32 pos[3], f32 rot[3], f32 scale[3], s32 arg[8],
                 s16 sw[5] (SW_FIELDS, -1 none), s16 group (GroupId, -1 none)}  (92 B)
  'PATH'  u32 n, n × {u16 zone, u16 closed, s32 id, u32 npts, u32 nraw, f32 pts[npts][3], f32 raw[nraw][3]}: pts the
          rail as a polyline (Bezier parts sampled, bezier_polyline), raw its JMap points (pnt0)
  'MODL'  u32 n, n × {char[32] name, f32 bounding radius}
  'KCL '  u32 n, n × {char[32] model, u32 size, bytes[size] (little-endian KCL, kcl_to_le), pad to 4}
  'VUMD'  u32 n, 12 pad, n × {char[32] name, u32 ngroups, f32 qscale, 8 pad, ngroups × {u32 nverts (3 per
          triangle), u16 tex, u16 nbatch, u8 blend (0 opaque, 1 blended, 2 additive), u8 z_write, u8 alpha, 5 pad, nbatch × {u16 emitted, u16 unique} padded to 16, then per batch
          (index_batches): s16 pos[unique][4] (x, y, z in steps of qscale, w = 4 * matrix slot), s16 uv[unique][2]
          if textured, u8 rgbi[emitted][4] (colour + unique index), each array padded to 16}}
  'GRAV'  u32 n, n × {u16 kind, u16 flags, s32 priority, s32 inverse, f32 range, f32 distant, f32 m[12] (x, y, z
          axes and translation, world), f32 p[4]}  (84 B): grav_record, Petari GravityCreator.cpp per kind
"""
import json
import math
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B
import camera
import geom
import skin
import tex

RESIDENT = ("Coin", "StarPiece", "SuperSpinDriver")  # OBJ_TYPES in phase 0b's RESIDENT set (out/sets.json): the resident pack
SKINNED = ("Mario",)
# ponytail: models the renderer cannot reproduce: the atmosphere sphere (its glow is a TEV rim effect; drawn with its
# additive material it floods the screen)
NO_DRAW = ("SphereAir",)   # skinned models converted with SKIN data (milestone 3); Mario's clips go in ANIM
OBJ_TYPES = ("Coin", "PurpleCoin", "StarPiece", "Kuribo", "CirclePurpleCoinGroup", "PurpleRailCoin", "CircleCoinGroup",
             "RailCoin", "SuperSpinDriver", "StarPieceGroup")
SW_FIELDS = ("SW_APPEAR", "SW_A", "SW_B", "SW_DEAD", "SW_SLEEP")  # stage switch ids: < 1000 the zone's, else global
ROW_FIELDS = ("pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z")
ROW = struct.Struct("<HHii9f8i5hh")
# second models a class draws besides its own (FireBar.cpp: FireBarBall around the FireBarCore)
EXTRA = {"FireBar": ("FireBarBall",)}
# model-less classes the engine runs (stage switch logic): kept in OBJS like the classes with a model
LOGIC = ("TimerSwitch", "SwitchSynchronizerReverse", "GroupSwitchWatcher", "CollisionBlocker", "FlipPanelObserver",
         "YellowChipGroup", "BlueChipGroup", "SwitchCube", "SwitchSphere", "SwitchCylinder", "DeathCube", "DeathSphere",
         "DeathCylinder")  # areas (areaobjinfo) travel as OBJS rows too


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
    sw = {f: int_or(b, f, n, -1) for f in SW_FIELDS}
    group = int_or(b, "GroupId", n, -1)
    out = []
    for i in range(n):
        out.append(dict(name=names[i], l_id=lid[i], path=path[i], pos=[fl[f][i] for f in ROW_FIELDS[:3]],
                        rot=[fl[f][i] for f in ROW_FIELDS[3:6]], scale=[fl[f][i] or 1.0 for f in ROW_FIELDS[6:]],
                        args=[a[i] for a in args], range=(float_column(b, "Range") or [-1.0] * n)[i],
                        distant=(float_column(b, "Distant") or [None] * n)[i],
                        priority=int_or(b, "Priority", n, 0)[i], inverse=int_or(b, "Inverse", n, 0)[i],
                        sw=[sw[f][i] for f in SW_FIELDS], group=group[i]))
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
    cubes, bcams = [], []  # camera areas (cameracubeinfo rows) and each stage's cameraparam.bcam
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
            for f in ("objinfo", "planetobjinfo", "areaobjinfo"):
                for r in rows(files.get(f"{root}/jmp/placement/{layer}/{f}", b"")):
                    objs.append(dict(r, zone=zid))
            for r in rows(files.get(f"{root}/jmp/start/{layer}/startinfo", b"")):
                starts.append(dict(r, zone=zid))
            cb = files.get(f"{root}/jmp/placement/{layer}/cameracubeinfo", b"")
            valid, sw_a = B.bcsv_column(cb, "Validity"), int_or(cb, "SW_A", len(B.bcsv_column(cb, "name")), -1)
            for i, r in enumerate(rows(cb)):
                cubes.append(dict(r, zone=zid, invalid=i < len(valid) and valid[i] == "Invalid", sw_a=sw_a[i]))
        bcams.append((zid, files.get(f"{root}/camera/cameraparam.bcam", b"")))
        info = files.get(f"{root}/jmp/path/commonpathinfo", b"")
        ids, closed = col(info, "l_id", len(B.bcsv_column(info, "name")), -1), B.bcsv_column(info, "closed")
        for i, pid in enumerate(ids):
            pts = files.get(f"{root}/jmp/path/commonpathpointinfo.{i}", b"")
            p0, p1, p2 = ([float_column(pts, f"pnt{k}_{a}") for a in "xyz"] for k in range(3))
            paths.append(dict(zone=zid, id=pid, closed=int(str(closed[i] if i < len(closed) else "").lower() == "close"),
                              pts=list(zip(*p0)), cin=list(zip(*p1)) or list(zip(*p0)), cout=list(zip(*p2)) or list(zip(*p0))))
    return act, zones, objs, starts, paths, cubes, bcams, up


def model_groups(arc, ratio, stats=None, skinned=False):
    """-> (groups [(verts, tris, uvs or None, texture or None, slots or None, colours or None, blend)], kcl bytes or
    None,
    skin chunk entry or None) for the first J3D model of an archive. texture = tex.to_ps2 dict of the group's material
    (tex.py rule); uvs per vertex after the material's texture matrix and the mirror halving. stats counts groups
    without texture / with projected coordinates. skinned: no simplification (vertices of one group sit in different
    joint spaces), per vertex the matrix slot (PNMTXIDX / 3) and the CLR0 colour, and the SKIN entry (skin_entry)."""
    groups, kcl, sk = [], None, None
    stats = stats if stats is not None else {}
    for name, b in B.rarc_files(arc.read_bytes()):
        n = name.lower()
        if n.endswith((".bdl", ".bmd")) and b[:4] == b"J3D2" and not groups:
            got = []
            geom.model(b, 1.0 if skinned else ratio, None, got)
            bl = tex.blocks(b)
            if skinned:
                tables = skin.group_tables(b, bl)
                gtab = []
                clr = geom.vtx1_color(b, bl[b"VTX1"])
            shapes = tex.shape_materials(b, bl[b"INF1"])
            texs = tex.textures(b, bl[b"TEX1"]) if b"TEX1" in bl else []
            ps2 = {}
            for pos, tri, info in got:
                if not tri:
                    continue
                used = sorted(set(tri))
                remap = {v: i for i, v in enumerate(used)}
                verts = [pos[3 * v:3 * v + 3] for v in used]
                mat = shapes.get(info["shape"])
                mt = tex.material_tex(b, bl[b"MAT3"], mat, bl.get(b"TEX1")) if mat is not None else None
                get = geom.vtx1_attr(b, bl[b"VTX1"], mt["attr"]) if mt and mt["attr"] in info["at"] else None
                uvs = t = None
                if get and mt["tex"] < len(texs):
                    if mt["tex"] not in ps2:
                        x = texs[mt["tex"]]
                        ps2[mt["tex"]] = tex.to_ps2(x["rgba"], x["wrap_s"], x["wrap_t"])
                    t = ps2[mt["tex"]]
                    off, w = info["at"][mt["attr"]]
                    m = mt["mtx"]
                    uvs = []
                    for v in used:
                        k = info["keys"][v]
                        su, sv = get(k[off] if w == 1 else k[off] << 8 | k[off + 1])
                        if m:
                            su, sv = (m[0][0] * su + m[0][1] * sv + m[0][2], m[1][0] * su + m[1][1] * sv + m[1][2])
                        uvs.append((su * t["u_scale"], sv * t["v_scale"]))
                    stats["projected"] = stats.get("projected", 0) + mt["projected"]
                else:
                    stats["untextured"] = stats.get("untextured", 0) + 1
                slots = cols = None
                if skinned:
                    at = info["at"]
                    slots = [info["keys"][v][at[0][0]] // 3 if 0 in at else 0 for v in used]
                    if clr and 11 in at:
                        o, w = at[11]
                        cols = [clr(info["keys"][v][o] if w == 1 else info["keys"][v][o] << 8 | info["keys"][v][o + 1])
                                for v in used]
                    gtab.append(tables[(info["shape"], info["group"])])
                blend = tex.material_blend(b, bl[b"MAT3"], mat) if mat is not None else (0, 1, 255)
                groups.append((verts, [remap[v] for v in tri], uvs, t, slots, cols, blend))
            if skinned:
                sk = skin_entry(b, bl, gtab)
        elif n.endswith(".kcl") and kcl is None:
            kcl = b
    return groups, kcl, sk


def skin_entry(b, bl, gtab):
    """SKIN chunk entry (after the model name): u16 njoints, ndrw, ngroups, nmix; joints {s16 parent, u16 pad,
    f32 scale[3], rot[3] (radians), trans[3]} (bind pose locals); drw {u16 first, u16 count} (count 0: rigid, first =
    joint; else a run of mix entries); mix {u16 joint, u16 pad, f32 weight, f32 invbind[3][4]}; groups {u8 nmat,
    u8 drw[11]} (skin.py)."""
    js, dr = skin.joints(b, bl), skin.draw_entries(b, bl)
    out, drw, mix = bytearray(), bytearray(), bytearray()
    nmix = 0
    for j, m in dr:
        if m is None:
            drw += struct.pack("<HH", j, 0)
        else:
            drw += struct.pack("<HH", nmix, len(m))
            for jj, w, ib in m:
                mix += struct.pack("<HHf12f", jj, 0, w, *ib[:3, :].reshape(-1))
                nmix += 1
    assert all(len(t) <= 11 and max(t) < 256 for t in gtab), "matrix table does not fit u8[11]"
    out += struct.pack("<4H", len(js), len(dr), len(gtab), nmix)
    for j in js:
        out += struct.pack("<hH9f", j["parent"], 0, *j["scale"], *j["rot"], *j["trans"])
    out += drw + mix
    for t in gtab:
        out += struct.pack("<B11B", len(t), *(list(t) + [0] * (11 - len(t))))
    return bytes(out) + b"\0" * (-len(out) % 4)


def anim_clip(bck):
    """BCK (ANK1) -> little-endian clip: {u16 frame_max, u8 attribute (loop mode), u8 dec_shift, u16 njoints,
    u16 nscale, u16 nrot, u16 ntrans, 4 pad}, njoints x 3 axes x {scale, rot, trans} x {u16 count, u16 offset,
    u16 type} (J3DAnmTransformKeyTable), then f32 scale[nscale], s16 rot[nrot] (pad to 4), f32 trans[ntrans].
    Offsets in the ANK1 block (J3DAnmLoader.cpp:303-313, J3DAnimation.hpp:306-317) are from the block start."""
    k = 0x20
    assert bck[k:k + 4] == b"ANK1", bck[k:k + 4]
    attr, shift = bck[k + 8], bck[k + 9]
    fmax, nj, ns, nr, nt = struct.unpack_from(">h4H", bck, k + 0xA)
    tab, so, ro, to = (k + B.u32(bck, k + x) for x in (0x14, 0x18, 0x1C, 0x20))
    table = struct.unpack_from(f">{nj * 27}H", bck, tab)
    out = struct.pack("<HBBHHHH4x", fmax, attr, shift, nj, ns, nr, nt) + struct.pack(f"<{nj * 27}H", *table)
    out += b"\0" * (-len(out) % 4)
    out += struct.pack(f"<{ns}f", *struct.unpack_from(f">{ns}f", bck, so))
    rot = struct.pack(f"<{nr}h", *struct.unpack_from(f">{nr}h", bck, ro))
    out += rot + b"\0" * (-len(rot) % 4)
    out += struct.pack(f"<{nt}f", *struct.unpack_from(f">{nt}f", bck, to))
    return out


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


UV_ONE = 4096  # UVs as s16 with 12 fraction bits (VU1 itof12): -8 .. +8 per triangle


def pos_scale(groups):
    """Quantisation step of a model's s16 positions: the largest coordinate maps to 32767."""
    m = max((abs(c) for verts, *_ in groups for v in verts for c in v), default=1.0)
    return (m or 1.0) / 32767.0


def vu_groups(groups, base, stats=None, qs=1.0):
    """Indexed groups -> [(pos s16x4, colour u8x4, uv s16x2)] per group, flat shading: k = 0.25 + 0.75 |n . L|.
    Positions are x, y, z in steps of qs (the engine folds qs into the matrix) and w = 4 * matrix slot (0: static).
    Textured groups get grey 0x80 * k (the GS modulates: 0x80 = 1.0), untextured ones base * k. Each triangle's UVs
    are shifted by whole repeats towards 0 on repeating axes so they fit s16; triangles still wider than 8 repeats
    are clamped and counted in stats["uv_clamped"]. Colour and UV arrays are padded to 16 bytes (DMA)."""
    ll = math.sqrt(sum(c * c for c in LIGHT))
    L = [c / ll for c in LIGHT]
    pad = lambda x: bytes(x) + b"\0" * (-len(x) % 16)
    out = []
    for verts, tris, uvs, t, slots, cols, _ in groups:
        pos, col, uv = bytearray(), bytearray(), bytearray()
        rgb = (0x80, 0x80, 0x80) if t else base
        for i in range(0, len(tris), 3):
            a, b, c = (verts[tris[i + k]] for k in range(3))
            e1 = [b[j] - a[j] for j in range(3)]
            e2 = [c[j] - a[j] for j in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            nl = math.sqrt(sum(x * x for x in n)) or 1.0
            k = 0.25 + 0.75 * abs(sum(n[j] * L[j] for j in range(3))) / nl
            rgba = struct.pack("<4B", int(rgb[0] * k), int(rgb[1] * k), int(rgb[2] * k), 0x80)
            for q in range(3):
                vi = tris[i + q]
                v = verts[vi]
                p = [max(-32767, min(32767, round(c / qs))) for c in v]
                if slots is None:   # static: slot 0
                    pos += struct.pack("<4h", *p, 0)
                    col += rgba
                else:               # skinned: w = 4 * matrix slot; colour = CLR0 scaled to 0x80, unlit
                    pos += struct.pack("<4h", *p, 4 * slots[vi])
                    cc = cols[vi] if cols else (255, 255, 255, 255)
                    col += struct.pack("<4B", *(x * 0x80 // 255 for x in cc[:3]), 0x80)
            if uvs:
                tri = [uvs[tris[i + k]] for k in range(3)]
                du = 0 if t["clamp_s"] else math.floor(min(x[0] for x in tri))
                dv = 0 if t["clamp_t"] else math.floor(min(x[1] for x in tri))
                for su, sv in tri:
                    q = [round((su - du) * UV_ONE), round((sv - dv) * UV_ONE)]
                    if not all(-32768 <= x < 32768 for x in q) and stats is not None:
                        stats["uv_clamped"] = stats.get("uv_clamped", 0) + 1
                    uv += struct.pack("<2h", *(max(-32768, min(32767, x)) for x in q))
        out.append((pad(pos), pad(col), pad(uv)))
    return out


BATCH = 72  # emitted vertices per VU1 batch (engine render.c BATCH)


def index_batches(pos, col, uv, n):
    """De-indexed group arrays (vu_groups) -> (batch table, data) of the indexed VUMD layout: per batch of BATCH
    emitted vertices, the unique (position, uv) pairs once, then per emitted vertex its colour with the unique index
    in the 4th byte (the alpha, always 0x80, is put back on VU1). -> (nbatch, bytes)."""
    pad = lambda x: bytes(x) + b"\0" * (-len(x) % 16)
    table, data = b"", b""
    for o in range(0, n, BATCH):
        ne = min(BATCH, n - o)
        keys, upos, uuv, cols = {}, bytearray(), bytearray(), bytearray()
        for e in range(o, o + ne):
            p = pos[8 * e:8 * e + 8]
            t = uv[4 * e:4 * e + 4] if uv else b""
            if p + t not in keys:
                keys[p + t] = len(keys)
                upos += p
                uuv += t
            k = keys[p + t]
            cols += col[4 * e:4 * e + 3] + bytes((k,))
        table += struct.pack("<HH", ne, len(keys))
        data += pad(upos) + (pad(uuv) if uv else b"") + pad(cols)
    return len(table) // 4, pad(table) + data


def chunk(tag, payload):
    payload += b"\0" * (-len(payload) % 16)
    return tag + struct.pack("<I8x", len(payload)) + payload


def name32(s):
    return s.encode("ascii", "replace")[:31].ljust(32, b"\0")


def rot_mtx(rot):
    """R = Rz * Ry * Rx of Euler angles in degrees (the engine's mat_translate_rot_scale), rows."""
    cx, sx, cy, sy, cz, sz = (f(math.radians(a)) for a in (rot[0], rot[1], rot[2]) for f in (math.cos, math.sin))
    return [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
            [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
            [-sy, cy * sx, cy * cx]]


def mtx_euler(r):
    """Inverse of rot_mtx -> degrees."""
    ry = math.asin(max(-1.0, min(1.0, -r[2][0])))
    if abs(r[2][0]) < 0.99999:
        rx, rz = math.atan2(r[2][1], r[2][2]), math.atan2(r[1][0], r[0][0])
    else:  # gimbal lock: put everything in x
        rx, rz = math.atan2(-r[1][2], r[1][1]), 0.0
    return [math.degrees(a) for a in (rx, ry, rz)]


def mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


BEZIER_STEPS = 8  # samples per curved rail part


def bezier_polyline(pts, cin, cout, closed):
    """Petari BezierRail: part i runs from pnt0[i] (control pnt2[i]) to pnt0[i+1] (control pnt1[i+1]); a part whose
    controls sit on its ends is straight. -> the rail as points along it (closed rails without the repeated start)."""
    n = len(pts)
    if n < 2:
        return list(pts)
    out = [pts[0]]
    for i in range(n if closed else n - 1):
        a, b, c, d = pts[i], cout[i], cin[(i + 1) % n], pts[(i + 1) % n]
        near = lambda u, v: sum((u[k] - v[k]) ** 2 for k in range(3)) < 1.0
        if near(a, b) and near(c, d):
            out.append(d)
        else:
            for s in range(1, BEZIER_STEPS + 1):
                t = s / BEZIER_STEPS
                w = ((1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3)
                out.append(tuple(w[0] * a[k] + w[1] * b[k] + w[2] * c[k] + w[3] * d[k] for k in range(3)))
    return out[:-1] if closed else out


def path_entry(p):
    dense = bezier_polyline(p["pts"], p.get("cin", p["pts"]), p.get("cout", p["pts"]), p["closed"])
    return (struct.pack("<HHiII", p["zone"], p["closed"], p["id"], len(dense), len(p["pts"])) +
            b"".join(struct.pack("<3f", *pt) for pt in dense) + b"".join(struct.pack("<3f", *pt) for pt in p["pts"]))


def to_world(zones, up_ids, items):
    """Zone-local rows (pos, rot) -> world, in place: world = zone placement composed up its chain of parents.
    Returns each zone's world (rotation rows, translation)."""
    world = {}

    def zw(z):
        if z not in world:
            r, t = rot_mtx(zones[z]["rot"]), zones[z]["pos"]
            if z in up_ids:
                pr, pt = zw(up_ids[z])
                r, t = mul3(pr, r), [pt[i] + sum(pr[i][k] * t[k] for k in range(3)) for i in range(3)]
            world[z] = (r, list(t))
        return world[z]
    for it in items:
        z = it["zone"]
        if z == 0 or (zones[z]["pos"] == [0, 0, 0] and zones[z]["rot"] == [0, 0, 0] and z not in up_ids):
            continue
        r, t = zw(z)
        if "pts" in it:
            for key in ("pts", "cin", "cout"):
                it[key] = [tuple(t[i] + sum(r[i][k] * pt[k] for k in range(3)) for i in range(3)) for pt in it.get(key, [])]
            continue
        it["pos"] = [t[i] + sum(r[i][k] * it["pos"][k] for k in range(3)) for i in range(3)]
        it["rot"] = mtx_euler(mul3(r, rot_mtx(it["rot"])))
    return [zw(z) for z in range(len(zones))]


GRAV_KINDS = {"GlobalPointGravity": 0, "GlobalPlaneGravity": 1, "GlobalPlaneGravityInBox": 2,
              "GlobalPlaneGravityInCylinder": 3, "GlobalCubeGravity": 4, "GlobalDiskGravity": 5,
              "GlobalSegmentGravity": 6}


def rotate_about(v, axis, deg):
    """Rodrigues: v rotated by deg around unit axis (TRot3f::setRotate + mult)."""
    a = math.radians(deg)
    c, sn = math.cos(a), math.sin(a)
    cr = [axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]]
    d = sum(axis[i] * v[i] for i in range(3))
    return [v[i] * c + cr[i] * sn + axis[i] * d * (1 - c) for i in range(3)]


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v] if n > 1e-9 else [0.0, 0.0, 0.0]


def grav_record(o):
    """One gravity row -> GRAV bytes, or None for kinds the engine has no evaluator for (Petari GravityCreator.cpp:
    settingFromSRT / settingFromJMapArgs, the world matrix baked in). m = x, y, z axes + translation; p, flags:
    plane (1-3): p0 base distance; box: m = range box (axes scaled to the half extents), flags = distance calc type
    (0 X, 1 Y, 2 Z, 3 default); cylinder: p1 radius, p2 height. cube (4): m = cube (axes = half extents), flags =
    active faces. disk (5): m.t centre, m.y normal, m.x the valid-sector direction, p0 radius, p1 valid cos, flags
    bit 0 both sides, bit 1 edge gravity. segment (6): m.t point 0, m.y unit axis, m.x valid-side direction, p0 axis
    length, p1 valid cos, flags bit 0/1 = edge 0/1 valid."""
    kind = GRAV_KINDS.get(o["name"])
    if kind is None:
        return None
    r, t, sc = rot_mtx(o["rot"]), list(o["pos"]), o["scale"]
    ax = [[r[i][k] for i in range(3)] for k in range(3)]  # unit axes x, y, z
    a = [x if -2**31 <= x < 2**31 else -1 for x in o["args"]]
    m, pr, flags = ax + [t], [0.0] * 4, 0
    distant = o["distant"]
    if distant is None:
        distant = 500.0 * sc[0] if kind == 0 else 0.0
    base = 2000.0 if a[0] < 0 else float(a[0])
    if kind == 1:
        pr[0] = 2000.0
    elif kind in (2, 4):  # box range / cube: TR, translation up by 500 sy, axes scaled by 500 s
        h = [500.0 * x for x in sc]
        m = [[c * h[k] for c in ax[k]] for k in range(3)] + [[t[i] + ax[1][i] * h[1] for i in range(3)]]
        if kind == 2:
            pr[0], flags = base, a[1] if a[1] in (0, 1, 2) else 3
        else:
            flags = (a[0] & 1) | (a[0] & 2) | (a[1] & 1) << 2 | (a[1] & 2) << 2 | (a[2] & 1) << 4 | (a[2] & 2) << 4
    elif kind == 3:
        pr[:3] = [base, 500.0 * sc[0], 500.0 * sc[1]]
    elif kind == 5:
        deg = float(a[2]) if a[2] >= 0 else 360.0
        side = unit([ax[0][i] - ax[1][i] * sum(ax[0][j] * ax[1][j] for j in range(3)) for i in range(3)])
        m[0] = rotate_about(side, ax[1], 0.5 * deg)
        pr[:2] = [500.0 * max(sc), math.cos(math.radians(0.5 * deg))]
        flags = (a[0] != 0) | (a[1] != 0) << 1
    elif kind == 6:  # TRS: the axis is 1000 * scale.y long, the side vector the (unit) X axis
        deg = float(a[1]) if a[1] >= 0 else 360.0
        side = unit([ax[0][i] - ax[1][i] * sum(ax[0][j] * ax[1][j] for j in range(3)) for i in range(3)])
        m[0] = rotate_about(side, ax[1], 0.5 * deg)
        pr[:2] = [1000.0 * abs(sc[1]), math.cos(math.radians(0.5 * deg))]
        flags = {0: 0, 1: 1, 2: 2}.get(a[0], 3)
    return struct.pack("<HHiiff12f4f", kind, flags, o["priority"], 1 if o["inverse"] == 1 else 0, o["range"], distant,
                       *[x for v in m for x in v], *pr)


_factory = None


def model_archive(fsp, name, planets):
    """Archive holding a placed class's model: the planet itself, else the first archive Petari's NameObjFactory gives
    the name (cCreateTable / cName2ArchiveNamesTable, budget.factory_tables), else the name; None if not on disc."""
    global _factory
    if name in planets or name in OBJ_TYPES or name in SKINNED:
        return name
    if name in NO_DRAW:
        return None
    if _factory is None:  # parsing Petari's sources takes minutes on the SD card: cached next to the other outputs
        cache = Path(__file__).parent.parent / "out" / "factory.json"
        if cache.exists():
            _factory = json.loads(cache.read_text())
        else:
            _factory = B.factory_tables()[1]
            cache.write_text(json.dumps(_factory))
    for a in _factory.get(name, []) + [name]:
        if (fsp / "ObjectData" / f"{a}.arc").exists():
            return a
    return None


def placement(fs, galaxy, scenario):
    """Scenario-wide chunks (TYPE ZONE OBJS STRT PATH GRAV CAMS CARE) and what the asset packs need."""
    act, zones, objs, starts, paths, cubes, bcams, up = scene(fs, galaxy, scenario)
    # everything placed in a zone goes to world space here, so the engine never composes zone matrices.
    # ponytail: CAMS vectors (fixed eye points, tower axes) stay zone-local; fine while zones sit at the origin
    # unrotated, transform them in camera.c by the CAMS zone when a galaxy needs it
    zid = {z["name"]: i for i, z in enumerate(zones)}
    up_ids = {zid[z]: zid[p] for z, p in up.items() if z in zid and p in zid and zid[p] != 0}
    zworld = to_world(zones, up_ids, objs + starts + paths + cubes)
    fsp = Path(fs)
    planets = B.planet_table(fsp, lambda a: (fsp / "ObjectData" / f"{a}.arc").exists())
    # every class with a model on disc is kept (static until it gets a behaviour), plus gravities
    arcmap = {n: model_archive(fsp, n, planets) for n in {o["name"] for o in objs}}
    keep = [o for o in objs if arcmap[o["name"]] or "Gravity" in o["name"] or o["name"] in LOGIC]
    for o in keep:
        for e in EXTRA.get(o["name"], ()):
            arcmap.setdefault(e, e if (fsp / "ObjectData" / f"{e}.arc").exists() else None)
    types = sorted({o["name"] for o in keep} | {s["name"] for s in starts})
    tid = {t: i for i, t in enumerate(types)}
    pack_row = lambda r: ROW.pack(tid[r["name"]], r["zone"], r["l_id"], r["path"], *r["pos"], *r["rot"], *r["scale"],
                                  *[a if -2**31 <= a < 2**31 else -1 for a in r["args"]],
                                  *[v if -2**15 <= v < 2**15 else -1 for v in r.get("sw", [-1] * 5)],
                                  r.get("group", -1) if -2**15 <= r.get("group", -1) < 2**15 else -1)
    out = [chunk(b"TYPE", struct.pack("<I", len(types)) + b"".join(name32(t) for t in types)),
           chunk(b"ZONE", struct.pack("<I", len(zones)) + b"".join(
               name32(z["name"]) + struct.pack("<6f", *t, *mtx_euler(r)) for z, (r, t) in zip(zones, zworld))),
           chunk(b"OBJS", struct.pack("<I", len(keep)) + b"".join(pack_row(r) for r in keep)),
           chunk(b"STRT", struct.pack("<I", len(starts)) + b"".join(pack_row(r) for r in starts)),
           chunk(b"PATH", struct.pack("<I", len(paths)) + b"".join(path_entry(p) for p in paths))]
    grav = [g for g in (grav_record(o) for o in keep) if g]
    out.append(chunk(b"GRAV", struct.pack("<I", len(grav)) + b"".join(grav)))
    out.append(chunk(b"ZPAR", struct.pack(f"<I{len(zones)}i", len(zones),
                                          *[-1 if i == 0 else up_ids.get(i, 0) for i in range(len(zones))])))
    cams, cam_index, cam_types = camera.cameras(bcams, float_column, int_or)
    care = [struct.pack("<BBH9f", camera.SHAPES[r["name"]], r["zone"],
                        cam_index.get((r["zone"], "c", r["args"][0] & 0xFFFF), 0xFFFF), *r["pos"], *r["rot"], *r["scale"])
            for r in camera.areas(cubes)]
    out.append(chunk(b"CAMS", struct.pack("<I12x", len(cams)) + b"".join(cams)))
    out.append(chunk(b"CARE", struct.pack("<I12x", len(care)) + b"".join(care)))
    # asset names per stage, in draw order: planets first, then object types
    names = [z["name"] for z in zones]
    per_stage = {st: [] for st in act}
    for o in keep:
        n = o["name"]
        for m in (n,) + EXTRA.get(n, ()):
            if arcmap[m] and m not in per_stage[names[o["zone"]]]:
                per_stage[names[o["zone"]]].append(m)
    for st in per_stage:
        per_stage[st].sort(key=lambda n: (n not in planets, n))
    info = dict(stages=act, types=types, objs=len(keep), starts=len(starts), paths=len(paths), cameras=len(cams),
                areas=len(care), camera_types=dict(cam_types))
    return out, info, dict(planets=planets, per_stage=per_stage, up=up, arcmap=arcmap)


_model_cache = {}


def assets(fsp, models, planets, ratio=0.99, stats=None, collision=True):
    """Asset chunks (TEXT MODL SKIN VUMD KCL) for [(model name, archive name)]: textures deduplicated within the pack.
    KCL for planets when `collision`. Converted models are cached per process (one archive, many scenarios)."""
    stats = stats if stats is not None else {}
    models_, kcls, vumd, skins = [], [], [], []
    texts, tex_id = [], {}
    for t, a in models:
        arc = fsp / "ObjectData" / f"{a}.arc"
        if not arc.exists():
            continue
        key = (a, ratio)
        if key not in _model_cache:
            st = {}
            _model_cache[key] = (model_groups(arc, ratio, st, t in SKINNED), st)
        (groups, kcl, sk), st = _model_cache[key]
        for k, v in st.items():
            stats[k] = stats.get(k, 0) + v
        if sk:
            skins.append(name32(t) + sk)
        # MODL keeps only the bounding radius (culling): drawing uses VUMD, collision KCL
        models_.append(name32(t) + struct.pack("<f", math.sqrt(max((sum(c * c for c in v) for verts, *_ in groups
                                                                    for v in verts), default=0.0))))
        vg, qs = b"", pos_scale(groups)
        for (_, tris, _, tx, _, _, bl), (pos, col, uv) in zip(groups, vu_groups(groups, BASE_COLOUR.get(t, (112, 176, 96)),
                                                                             stats, qs)):
            ti = 0xFFFF
            if tx:
                key = tx["pix"] + tx["clut"] + bytes((tx["clamp_s"], tx["clamp_t"]))
                ti = tex_id.setdefault(key, len(texts))
                if ti == len(texts):
                    texts.append(tx)
            nb, body = index_batches(pos, col, uv, len(tris))
            vg += struct.pack("<IHHBBB5x", len(tris), ti, nb, *bl) + body
        vumd.append(name32(t) + struct.pack("<If8x", len(groups), qs) + vg)
        if kcl and collision:
            kcl = kcl_to_le(kcl)
            kcls.append(name32(t) + struct.pack("<I", len(kcl)) + kcl + b"\0" * (-len(kcl) % 4))
    head, data = bytearray(), bytearray()
    base = 16 + 32 * len(texts)
    for tx in texts:
        pix = base + len(data)
        data += tx["pix"] + b"\0" * (-len(tx["pix"]) % 16)
        clut = base + len(data)
        data += tx["clut"]
        head += struct.pack("<HHBBBBIIIBB10x", tx["w"], tx["h"], tx["psm"], tx["clamp_s"], tx["clamp_t"], tx["alpha"],
                            pix, len(tx["pix"]), clut, tx["w"].bit_length() - 1, tx["h"].bit_length() - 1)
    stats.update(models=stats.get("models", 0) + len(models_), kcls=stats.get("kcls", 0) + len(kcls),
                 textures=stats.get("textures", 0) + len(texts))
    return [chunk(b"TEXT", struct.pack("<I12x", len(texts)) + bytes(head) + bytes(data)),
            chunk(b"MODL", struct.pack("<I", len(models_)) + b"".join(models_)),
            chunk(b"SKIN", struct.pack("<I", len(skins)) + b"".join(skins)),
            chunk(b"VUMD", struct.pack("<I12x", len(vumd)) + b"".join(vumd)),
            chunk(b"KCL ", struct.pack("<I", len(kcls)) + b"".join(kcls))]


def anim_chunk(fsp):
    clips = []
    for n, b in B.rarc_files((fsp / "ObjectData" / "MarioAnime.arc").read_bytes()):
        if n.lower().endswith(".bck"):
            raw = anim_clip(b)
            z = zlib.compress(raw, 9)
            clips.append(name32(n.rsplit("/", 1)[-1][:-4].lower()) + struct.pack("<II", len(raw), len(z)) + z +
                         b"\0" * (-len(z) % 4))
    return chunk(b"ANIM", struct.pack("<I", len(clips)) + b"".join(clips))


def blob_of(chunks):
    blob = b"SMGS" + struct.pack("<II4x", 5, len(chunks)) + b"".join(chunks)
    return blob + b"\0" * (-len(blob) % 16)


def build(fs, galaxy, scenario, ratio=0.99):
    """Everything in one blob: placement, every placed model in full, Mario and his clips (the phase 1 layout)."""
    out, info, ctx = placement(fs, galaxy, scenario)
    fsp, stats = Path(fs), {}
    types = info["types"]
    am = ctx["arcmap"]
    names = sorted({n for ns in ctx["per_stage"].values() for n in ns}, key=lambda t: (t not in ctx["planets"], t))
    out += assets(fsp, [(t, am[t]) for t in names] + [(t, t) for t in SKINNED], ctx["planets"], ratio, stats) + \
        [anim_chunk(fsp)]
    return blob_of(out), dict(info, **stats)


def low_lod(fsp, name):
    """Lowest LOD archive of a model (budget.py stream rule): Low, else Middle, else the model itself."""
    return next(a for a in (name + "Low", name + "Middle", name) if (fsp / "ObjectData" / f"{a}.arc").exists()
                or a == name)


def packs(fs, galaxy, scenario, resident, ratio=0.99):
    """Streaming layout of one scenario (phase 0b `stream`): -> ({file name: blob}, info).
    scene.bin   placement chunks of every active stage
    common.bin  models placed in two or more stages (always loaded)
    <Stage>.bin models placed only in that stage, in full, with collision (loaded while the stage is on the zone chain)
    <Stage>.low.bin  that stage's planets at their lowest LOD, no collision, no actors (loaded otherwise)
    Models in `resident` (the resident pack: Mario, coins, ...) are left out.
    info["stream"] = {stage: bytes loaded while the player is in it}: scene + common + its chain in full + the other
    stages low. ponytail: shared models always resident in common.bin; per-zone dedup at run time if that is too big."""
    out, info, ctx = placement(fs, galaxy, scenario)
    fsp, planets, per, up, am = Path(fs), ctx["planets"], ctx["per_stage"], ctx["up"], ctx["arcmap"]
    count = {}
    for st, ns in per.items():
        for n in ns:
            if n not in resident:
                count[n] = count.get(n, 0) + 1
    shared = sorted(n for n, c in count.items() if c > 1)
    files = {"scene.bin": blob_of(out),
             "common.bin": blob_of(assets(fsp, [(n, am[n]) for n in shared], planets, ratio))}
    for st, ns in per.items():
        own = [n for n in ns if count.get(n) == 1]
        files[f"{st}.bin"] = blob_of(assets(fsp, [(n, am[n]) for n in own], planets, ratio))
        files[f"{st}.low.bin"] = blob_of(assets(fsp, [(n, low_lod(fsp, n)) for n in own if n in planets], planets,
                                                ratio, collision=False))
    stream = {}
    for z in per:
        chain = [z]
        while chain[-1] in up:
            chain.append(up[chain[-1]])
        chain = set(chain) | {galaxy}
        stream[z] = len(files["scene.bin"]) + len(files["common.bin"]) + sum(
            len(files[f"{st}.bin" if st in chain else f"{st}.low.bin"]) for st in per)
    info.update(stream=stream, whole=sum(len(b) for f, b in files.items() if not f.endswith(".low.bin")))
    return files, info


def resident_pack(fs, ratio=0.99):
    """Models every scenario shares (phase 0b RESIDENT set, the ones the converter knows) + Mario + his clips."""
    fsp = Path(fs)
    names = list(RESIDENT) + list(SKINNED)
    return blob_of(assets(fsp, [(t, t) for t in names], {}, ratio) + [anim_chunk(fsp)]), set(names)


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
    # Bezier rails: a straight part stays two points, a curved one is sampled (midpoint of this arch: y = 37.5)
    line = [(0, 0, 0), (100, 0, 0)]
    assert bezier_polyline(line, line, line, 0) == line
    arch = bezier_polyline(line, [(0, 0, 0), (100, 50, 0)], [(0, 50, 0), (100, 0, 0)], 0)
    assert len(arch) == BEZIER_STEPS + 1 and abs(arch[BEZIER_STEPS // 2][1] - 37.5) < 1e-6, arch
    # round trip of the chunk framing
    blob = b"SMGS" + struct.pack("<II4x", 2, 2) + chunk(b"TYPE", struct.pack("<I", 1) + name32("Coin")) + chunk(b"KCL ", b"x")
    c = read_chunks(blob)
    assert c[b"TYPE"][4:8] == b"Coin" and c[b"KCL "][:1] == b"x", c
    for rot in ([10, 20, 30], [-45, 80, 170], [0, 0, 90]):
        assert all(abs(x - y) < 1e-6 for x, y in zip(mtx_euler(rot_mtx(rot)), rot)), rot
    zs = [dict(pos=[0, 0, 0], rot=[0, 0, 0]), dict(pos=[100, 0, 0], rot=[0, 90, 0])]
    it = [dict(zone=1, pos=[10, 0, 0], rot=[0, 0, 0]), dict(zone=1, pts=[(0, 0, 10)])]
    to_world(zs, {}, it)
    assert all(abs(x - y) < 1e-6 for x, y in zip(it[0]["pos"], [100, 0, -10])), it[0]
    assert abs(it[0]["rot"][1] - 90) < 1e-6 and abs(it[1]["pts"][0][0] - 110) < 1e-6, it
    g = grav_record(dict(name="GlobalCubeGravity", pos=[0, 0, 0], rot=[0, 0, 0], scale=[2, 1, 1], args=[3, 1, 2, -1],
                         distant=None, range=-1.0, priority=0, inverse=0))
    v = struct.unpack("<HHiiff12f4f", g)
    assert v[1] == 0b100111 and v[6:9] == (1000.0, 0.0, 0.0) and v[15:18] == (0.0, 500.0, 0.0), v
    # two triangles sharing an edge: 6 emitted, 4 unique; colours keep their rgb, the 4th byte is the index
    P = [struct.pack("<4h", *v, 0) for v in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0))]
    nb, body = index_batches(b"".join(P), b"\x10\x20\x30\x80" * 6, b"", 6)
    assert nb == 1 and struct.unpack_from("<HH", body, 0) == (6, 4), body[:4]
    assert body[16 + 32 + 3::4][:6] == bytes((0, 1, 2, 1, 2, 3)), body
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
