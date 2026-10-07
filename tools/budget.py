"""Per-galaxy memory budget: original Wii asset sizes vs. estimated PS2-converted sizes.

usage: python tools/budget.py data/fs/files [out_dir] [--levers]    (nodtool extract puts the game tree in files/)
       python tools/budget.py --selftest
Every format detail below cites docs/sources.md (Petari file:line).
"""
import csv, json, re, struct, sys, zlib
from pathlib import Path

def u16(b, o): return struct.unpack_from(">H", b, o)[0]
def u32(b, o): return struct.unpack_from(">I", b, o)[0]

# ---------- Yaz0 (JKRDecomp.cpp:195-260) ----------
def yaz0(src):
    if src[:4] != b"Yaz0":
        return src
    size = u32(src, 4)
    dst = bytearray()
    si = 16
    while len(dst) < size:
        code = src[si]; si += 1
        for bit in range(8):
            if len(dst) >= size:
                break
            if code & (0x80 >> bit):
                dst.append(src[si]); si += 1
                continue
            b1, b2 = src[si], src[si + 1]; si += 2
            dist = ((b1 & 0x0F) << 8 | b2) + 1
            n = b1 >> 4
            if n == 0:
                n = src[si] + 0x12; si += 1
            else:
                n += 2
            start = len(dst) - dist
            if dist >= n:
                dst += dst[start:start + n]
            else:  # overlapping run
                for i in range(n):
                    dst.append(dst[start + i])
    return bytes(dst)

# ---------- RARC (JKRArchive.hpp:17-73, JKRCompArchive.cpp:117-123) ----------
def rarc_files(arc):
    """Yield (name, bytes) for every file in a (decompressed) RARC."""
    arc = yaz0(arc)
    if arc[:4] != b"RARC":
        return
    info = u32(arc, 0x08)                  # mHeaderSize: info block follows the header
    data_off = info + u32(arc, 0x0C)       # mHeaderSize + mFileDataOffset
    n_entries, entry_off = u32(arc, info + 0x08), info + u32(arc, info + 0x0C)
    str_off = info + u32(arc, info + 0x14)
    for i in range(n_entries):
        e = entry_off + i * 0x14
        type_name = u32(arc, e + 4)
        if (type_name >> 24) & 0x02:  # FILE_FLAG_FOLDER
            continue
        name_off = type_name & 0xFFFFFF
        name = arc[str_off + name_off: arc.index(b"\0", str_off + name_off)].decode("shift_jis", "replace")
        off, size = u32(arc, e + 8) + data_off, u32(arc, e + 12)
        yield name, yaz0(arc[off:off + size])

# ---------- textures ----------
# GX texture format -> tile shift (x, y); port of __GXGetTexTileShift (GXTexture.c:69-113)
GX_TILE_SHIFT = {0: (3, 3), 8: (3, 3), 14: (3, 3),                      # I4 C4 CMPR
                 1: (3, 2), 2: (3, 2), 9: (3, 2),                       # I8 IA4 C8
                 3: (2, 2), 4: (2, 2), 5: (2, 2), 6: (2, 2), 10: (2, 2)}  # IA8 565 5A3 RGBA8 C14X2
PS2_4BPP = {0, 8, 14}  # D (sources.md): 4-bpp GX formats -> PSMT4, everything else -> PSMT8
PS2_MAX_DIM = 256      # D (sources.md): budget lever, GS itself allows 1024
# --levers (docs/phase0-report.md "Levers"): textures max 128 + all PSMT4, drop high LOD when Middle exists,
# kcl normals f32 -> s16, bck kept zlib-9 in RAM. All D; quality/precision/CPU cost not evaluated.
LEVERS = False

def gx_tex_size(fmt, w, h, mips):
    """Port of GXGetTexBufferSize (GXTexture.c:117-152); `mips` = ResTIMG image count."""
    sx, sy = GX_TILE_SHIFT.get(fmt, (0, 0))
    tile = 64 if fmt == 6 else 32
    total = 0
    for _ in range(max(mips, 1)):
        total += ((w + (1 << sx) - 1) >> sx) * ((h + (1 << sy) - 1) >> sy) * tile
        if w == 1 and h == 1:
            break
        w, h = max(w >> 1, 1), max(h >> 1, 1)
    return total

def ps2_tex_size(fmt, w, h, mips):
    max_dim = 128 if LEVERS else PS2_MAX_DIM
    bpp = 4 if LEVERS or fmt in PS2_4BPP else 8
    mips = max(mips, 1)
    while (w > max_dim or h > max_dim) and mips > 1:          # drop top mip levels
        w, h, mips = max(w // 2, 1), max(h // 2, 1), mips - 1
    while w > max_dim or h > max_dim:                         # no mips left: downscale
        w, h = max(w // 2, 1), max(h // 2, 1)
    total = 0
    for _ in range(mips):
        total += w * h * bpp // 8
        w, h = max(w // 2, 1), max(h // 2, 1)
    return total + (64 if bpp == 4 else 1024)

def bti_headers(b, base, count):
    """Yield (fmt, w, h, mips, abs_data_offset) for `count` 0x20-byte BTI headers at `base`."""
    for i in range(count):
        h = base + i * 0x20
        yield b[h], u16(b, h + 2), u16(b, h + 4), b[h + 0x18], h + u32(b, h + 0x1C)

# ---------- J3D (bmd/bdl): J3DModelLoader.hpp:49-77,187-191 ----------
def j3d_sizes(b):
    """-> dict tex_gx, tex_ps2, geo_gx, geo_ps2, other."""
    r = dict(tex_gx=0, tex_ps2=0, geo_gx=0, geo_ps2=0, other=0, other_ps2=0)
    o = 0x20
    for _ in range(u32(b, 0x0C)):
        tag, size = b[o:o + 4], u32(b, o + 4)
        if tag == b"TEX1":
            seen = set()
            for fmt, w, h, mips, data in bti_headers(b, o + u32(b, o + 0x0C), u16(b, o + 8)):
                if data in seen:
                    continue
                seen.add(data)
                r["tex_gx"] += gx_tex_size(fmt, w, h, mips)
                r["tex_ps2"] += ps2_tex_size(fmt, w, h, mips)
        elif tag == b"VTX1":
            r["geo_gx"] += size                     # PS2 side is counted per emitted vertex in SHP1
        elif tag == b"SHP1":
            from geom import shp1                   # de-indexed: every emitted vertex carries its data (geom.py)
            r["geo_gx"] += size
            r["geo_ps2"] += shp1(b, o)[1]
        else:
            r["other"] += size
            r["other_ps2"] += size
        o += size
    return r

def file_sizes(name, b):
    n = name.lower()
    if n.endswith((".bmd", ".bdl")) and b[:4] == b"J3D2":
        return j3d_sizes(b)
    if n.endswith(".bti"):
        fmt, w, h, mips, _ = next(bti_headers(b, 0, 1))
        return dict(tex_gx=gx_tex_size(fmt, w, h, mips), tex_ps2=ps2_tex_size(fmt, w, h, mips), geo_gx=0, geo_ps2=0, other=0, other_ps2=0)
    ps2 = len(b)
    if LEVERS and n.endswith(".kcl"):
        # normals sit between mNormOffset and the first prism (1-based: KCollision.cpp:67); f32 -> s16 halves them
        ps2 -= (u32(b, 8) + 0x10 - u32(b, 4)) // 2
    elif LEVERS and n.endswith(".bck"):
        ps2 = len(zlib.compress(b, 9))
    return dict(tex_gx=0, tex_ps2=0, geo_gx=0, geo_ps2=0, other=len(b), other_ps2=ps2)

# ---------- BCSV (JMap): JMapInfo.hpp:6-31, JMapInfo.cpp:55-70 ----------
def jmap_hash(s):  # JGadget hashcode.cpp:7-13
    h = 0
    for c in s.encode("shift_jis"):
        h = (h * 31 + c) & 0xFFFFFFFF
    return h

def bcsv_column(b, field):
    """Values of column `field` (str, or int for LONG/SHORT/BYTE), or [] if absent / not a BCSV."""
    if len(b) < 16:
        return []
    n, nf, data, esize = struct.unpack_from(">4I", b, 0)
    if not (0 < nf < 256 and 16 + nf * 12 <= data <= len(b) and data + n * esize <= len(b)):
        return []
    want = jmap_hash(field)
    for i in range(nf):
        fh, mask, foff, shift, ftype = struct.unpack_from(">IIHBB", b, 16 + i * 12)
        if fh != want:
            continue
        strings = data + n * esize
        out = []
        for e in range(n):
            base = data + e * esize + foff
            if ftype in (0, 3, 4, 5):  # LONG, LONG_2, SHORT, BYTE: (raw & mask) >> shift
                raw = struct.unpack_from({0: ">I", 3: ">I", 4: ">H", 5: ">B"}[ftype], b, base)[0]
                out.append((raw & mask) >> shift)
                continue
            if ftype == 6:
                s = strings + u32(b, base)
            elif ftype == 1:
                s = base
            else:
                return []
            end = b.find(b"\0", s)
            out.append(b[s:end].decode("shift_jis", "replace"))
        return out
    return []

# ---------- resident set: StationedFileInfo.cpp ----------
STATIONED_CPP = Path(__file__).resolve().parent.parent / "vendor/Petari/src/Game/System/StationedFileInfo.cpp"

def stationed_paths():
    """Archive paths the game keeps loaded, minus the Luigi-only set (only one player is loaded)."""
    src = STATIONED_CPP.read_text(encoding="utf-8")
    entries = re.findall(r"LOAD_TYPE_(\w+),\s*\"([^\"]+)\"", src)
    return list(dict.fromkeys(p for t, p in entries if t != "MOUNT_RESOURCE_LUIGI"))

# ---------- placement name -> mounted archives: NameObjFactory::getMountObjectArchiveList ----------
PETARI_SRC = STATIONED_CPP.parents[2]
FACTORY_CPP = PETARI_SRC / "Game/NameObj/NameObjFactory.cpp"
PLANET_SUFFIXES = ("Low", "Middle", "Bloom", "Water", "Indirect")  # PlanetMapCreator.cpp:353-357

def factory_tables():
    """-> (created: lowercase names the factory can create, by_name: name -> archive names)."""
    src = FACTORY_CPP.read_text(encoding="utf-8")
    i, j, k = (src.index(t) for t in ("cCreateTable[]", "cName2ArchiveNamesTable[]", "cName2MakeArchiveListFuncTable[]"))
    created, by_name = set(), {}
    # create table: {name, func, archive|nullptr}; name match is case-insensitive (getName2CreateFunc)
    for name, arc in re.findall(r'\{\s*"([^"]+)",[^{}"]*?,\s*(nullptr|"[^"]*"),?\s*\}', src[i:j]):
        created.add(name.lower())
        if arc != "nullptr":
            by_name.setdefault(name, []).append(arc.strip('"'))
    for name, arc in re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",?\s*\}', src[j:k]):
        by_name.setdefault(name, []).append(arc)
    # makeArchiveList funcs also branch on placement args; every literal reachable for the name counts (upper bound, sources.md E)
    bodies = {}
    for f in PETARI_SRC.rglob("*.cpp"):
        t = f.read_text(encoding="utf-8", errors="replace")
        # body ends at the closing brace with the definition's own indent
        for m in re.finditer(r"^( *)\w[\w ]*? (?:(\w+)::)?(\w+)\(NameObjArchiveListCollector\*[^)]*\)\s*\{(.*?)\n\1\}\n", t, re.S | re.M):
            # top-level `if (isEqualObjectName(rIter, "X") ...)` blocks only apply to X (MorphItemObjNeo.cpp:37-86)
            parts = []
            for seg in re.split(rf"\n{m.group(1)}    (?=if \()", m.group(4)):
                head = seg.split("\n", 1)[0]
                guard = set(re.findall(r'isEqualObjectName\(rIter, "([^"]+)"\)', head)) if head.startswith("if (") else set()
                parts.append((guard, [a for a in re.findall(r'addArchive\("([^"]+)"\)', seg) if "Luigi" not in a]))
            bodies[f"{m.group(2)}::{m.group(3)}" if m.group(2) else m.group(3)] = parts
    for name, func in re.findall(r'\{\s*"([^"]+)",\s*([\w:]+),?\s*\}', src[k:]):
        for guard, arcs in bodies.get(func, bodies.get(func.split("::")[-1], [])):
            if not guard or name in guard:
                by_name.setdefault(name, []).extend(arcs)
    return created, by_name

def planet_table(fs, exists):
    """PlanetName -> archives (PlanetMapCreator::makeArchiveListPlanet, .cpp:303-338). ForceLow ignored: high models counted."""
    arc = fs / "ObjectData/PlanetMapDataTable.arc"
    b = {n.lower(): d for n, d in rarc_files(arc.read_bytes())}["planetmapdatatable.bcsv"]
    names = bcsv_column(b, "PlanetName")
    out = {n: [n] for n in names}
    for suf in PLANET_SUFFIXES:
        for n, flag in zip(names, bcsv_column(b, suf + "Flag")):
            if flag and exists(n + suf):  # flag != 0 and MR::isExistModel (.cpp:24, 359-363)
                out[n].append(n + suf)
    return out

# ---------- disc walk ----------
KEYS = ("tex_gx", "tex_ps2", "geo_gx", "geo_ps2", "other", "other_ps2")

def arc_stats(path, cache):
    key = f"{path}:{path.stat().st_mtime_ns}:{LEVERS}"
    if key not in cache:
        tot = dict.fromkeys(KEYS, 0)
        names = set()
        zones = []
        for name, b in rarc_files(path.read_bytes()):
            for k, v in file_sizes(name, b).items():
                tot[k] += v
            names.update(bcsv_column(b, "name"))
            zones += bcsv_column(b, "ZoneName")
        cache[key] = dict(sizes=tot, names=sorted(names), zones=zones)
    return cache[key]

def row(label, parts):
    r = dict.fromkeys(KEYS, 0)
    for p in parts:
        for k in KEYS:
            r[k] += p["sizes"][k]
    r["wii_total"] = r["tex_gx"] + r["geo_gx"] + r["other"]
    r["ps2_total"] = r["tex_ps2"] + r["geo_ps2"] + r["other_ps2"]
    return dict(name=label, **r)

def main(fs, out):
    fs, out = Path(fs), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cache_file = out / "cache.json"
    cache = json.loads(cache_file.read_text()) if cache_file.exists() else {}
    stage, objdir = fs / "StageData", fs / "ObjectData"
    # archives mount by object or layout name (NameObjFactory.cpp requestMountObjectArchives); localized layout wins
    objects = {p.stem.lower(): p for d in (fs / "LayoutData", objdir, fs / "UsEnglish/LayoutData") for p in d.glob("*.arc")}
    created, by_name = factory_tables()
    planets = planet_table(fs, lambda a: a.lower() in objects)
    zone_arcs = {p.stem.lower() for p in stage.glob("*.arc")}

    def resolve(names):
        """-> (lowercase archives on disc, placement names the factory cannot create)."""
        arcs, unknown = set(), set()
        for n in names:
            if n in planets:
                arcs.update(planets[n])
            elif n.lower() in created or n in by_name:
                arcs.update(by_name.get(n, []))
            elif n.lower() not in zone_arcs:  # sub-zone names are counted through ZoneList
                unknown.add(n)
        return {a.lower() for a in arcs if a.lower() in objects}, unknown

    # ponytail: LOD archives counted whenever they exist (LodCtrl.cpp:351-374), over-counts actors without LOD
    lod = lambda names: {v for n in names for v in (n, n + "low", n + "middle") if v in objects
                         and not (LEVERS and v == n and n + "middle" in objects)}  # lever: Middle becomes top
    # language-prefixed path first, then plain (FileUtil.cpp:187-193); UsEnglish is our pick (sources.md, D)
    loc = lambda p: next((q for q in (fs / "UsEnglish" / p.lstrip("/"), fs / p.lstrip("/")) if q.exists()), fs / p.lstrip("/"))
    resident = [loc(p) for p in stationed_paths()]
    resident_row = row("<resident>", [arc_stats(p, cache) for p in resident if p.exists()])
    galaxies, zones = [dict(**resident_row, zones=0, unresolved=sum(not p.exists() for p in resident))], []
    try:
        for scen in sorted(stage.glob("*/*Scenario.arc")):
            gname = scen.name[: -len("Scenario.arc")]
            print(gname, flush=True)
            # a galaxy loads exactly its ZoneList entries (StageFileLoader.cpp:14-40)
            zone_names = list(dict.fromkeys(arc_stats(scen, cache)["zones"]))
            g_parts, g_objs, unresolved = [], set(), set()
            for z in zone_names:
                zp = stage / f"{z}.arc"
                if not zp.exists():
                    unresolved.add(z)
                    continue
                zs = arc_stats(zp, cache)
                arcs, unknown = resolve(zs["names"])
                z_parts = [zs] + [arc_stats(objects[n], cache) for n in sorted(lod(arcs))]
                zones.append(dict(galaxy=gname, **row(z, z_parts), unresolved=len(unknown)))
                g_parts.append(zs)
                g_objs |= lod(arcs)
                unresolved |= unknown
            g_parts += [arc_stats(objects[n], cache) for n in sorted(g_objs)]
            galaxies.append(dict(**row(gname, g_parts), zones=len(zone_names), unresolved=len(unresolved)))
    finally:
        cache_file.write_text(json.dumps(cache))
    for fname, rows in (("budget.csv", galaxies), ("zones.csv", zones)):
        rows.sort(key=lambda r: -r["ps2_total"])
        if rows:
            with open(out / fname, "w", newline="") as f:
                w = csv.DictWriter(f, fieldnames=rows[0].keys())
                w.writeheader(); w.writerows(rows)
    mb = lambda x: f"{x / 2**20:.1f}"
    for r in galaxies[:10]:
        print(f"{r['name']:40} wii {mb(r['wii_total']):>6} MB  ps2 {mb(r['ps2_total']):>6} MB  unresolved {r['unresolved']}")

def selftest():
    # Yaz0: "abc" literal, then back-ref dist 3 len 6 -> "abcabcabc"
    src = b"Yaz0" + struct.pack(">I", 9) + bytes(8) + bytes([0b11100000]) + b"abc" + bytes([0x40, 0x02])
    assert yaz0(src) == b"abcabcabc", yaz0(src)
    # spec scenario: 256x256 CMPR, no mips -> 32768 + 64
    assert ps2_tex_size(14, 256, 256, 1) == 32768 + 64
    assert gx_tex_size(14, 256, 256, 1) == 32768
    # 512x512 RGBA8 with 2 mips -> top level dropped -> 256x256 CLUT8 single level
    assert ps2_tex_size(6, 512, 512, 2) == 65536 + 1024
    global LEVERS
    LEVERS = True  # same texture under --levers: 128x128 PSMT4
    assert ps2_tex_size(6, 512, 512, 2) == 128 * 128 // 2 + 64
    LEVERS = False
    # BCSV: 2 entries, 1 string-offset field "name"
    strings = b"Kuribo\0Coin\0"
    hdr = struct.pack(">4I", 2, 1, 16 + 12, 4) + struct.pack(">IIHBB", jmap_hash("name"), 0xFFFFFFFF, 0, 0, 6)
    b = hdr + struct.pack(">II", 0, 7) + strings
    assert bcsv_column(b, "name") == ["Kuribo", "Coin"], bcsv_column(b, "name")
    assert bcsv_column(b, "ZoneName") == []
    print("selftest ok")

if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        args = [a for a in sys.argv[1:] if a != "--levers"]
        LEVERS = "--levers" in sys.argv
        main(args[0], args[1] if len(args) > 1 else "out")
