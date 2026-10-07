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
def rarc_files(arc, paths=False):
    """Yield (name, bytes) for every file in a (decompressed) RARC; with paths, name is 'dir/sub/file'."""
    arc = yaz0(arc)
    if arc[:4] != b"RARC":
        return
    info = u32(arc, 0x08)                  # mHeaderSize: info block follows the header
    data_off = info + u32(arc, 0x0C)       # mHeaderSize + mFileDataOffset
    n_dirs, dir_off = u32(arc, info), info + u32(arc, info + 0x04)
    entry_off = info + u32(arc, info + 0x0C)
    str_off = info + u32(arc, info + 0x14)
    cstr = lambda o: arc[str_off + o: arc.index(b"\0", str_off + o)].decode("shift_jis", "replace")
    dir_path = {0: cstr(u32(arc, dir_off + 4))}  # SDIDirEntry {id, name, hash, nFiles, first} (JKRArchive.hpp:75-81)
    for d in range(n_dirs):
        de = dir_off + d * 0x10
        first, count = u32(arc, de + 0x0C), u16(arc, de + 0x0A)
        for i in range(first, first + count):
            e = entry_off + i * 0x14
            type_name = u32(arc, e + 4)
            name = cstr(type_name & 0xFFFFFF)
            if (type_name >> 24) & 0x02:  # FILE_FLAG_FOLDER: mDirIndex at +8
                if name not in (".", ".."):
                    dir_path[u32(arc, e + 8)] = dir_path[d] + "/" + name
                continue
            off, size = u32(arc, e + 8) + data_off, u32(arc, e + 12)
            yield (dir_path[d] + "/" + name if paths else name), yaz0(arc[off:off + size])

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
VU_POOL = None    # --vu-index=N: geometry kept indexed, expanded on VU1 in batches of <= N unique vertices (geom.py)
SCENARIO = False  # --scenario: also write scenarios.csv (per galaxy and scenario, layer-masked)

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
            r["geo_ps2"] += shp1(b, o, VU_POOL)[1]
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
    key = f"{path}:{path.stat().st_mtime_ns}:{LEVERS}" + (f":vu{VU_POOL}" if VU_POOL else "")
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

LAYERS = ["common"] + [f"layer{c}" for c in "abcdefghijklmnop"]  # cLayerDirName, StageDataHolder.cpp:31-32

def arc_layers(path, cache):
    """Placement data per layer: {layer: {"names": [...], "zones": [...]}} plus "child" names (all layers)."""
    key = f"layers:{path}:{path.stat().st_mtime_ns}"
    if key not in cache:
        lay, child = {}, set()
        for p, b in rarc_files(path.read_bytes(), paths=True):
            parts = p.lower().split("/")
            if len(parts) != 5 or parts[1] != "jmp":
                continue
            cat, layer, fname = parts[2:]
            if cat == "childobj":  # loaded from every layer (initAllLayerJmpInfo), spawned by parents
                child.update(bcsv_column(b, "name"))
            elif cat in ("placement", "mapparts"):  # only these are layer-masked (initLayerJmpInfo)
                d = lay.setdefault(layer, {"names": [], "zones": []})
                names = bcsv_column(b, "name")
                d["zones" if fname == "stageobjinfo" else "names"] += names
        cache[key] = dict(layers=lay, child=sorted(child))
    return cache[key]

def scenario_masks(b):
    """ScenarioData.bcsv -> {scenarioNo: callable(stage) -> layer mask} (ScenarioDataParser.cpp:217-231)."""
    out = {}
    for i, no in enumerate(bcsv_column(b, "ScenarioNo")):
        out[no] = lambda stage, i=i: 1 | ((bcsv_column(b, stage) or [0] * (i + 1))[i] << 1)
    return out

def active_stages(main, mask_of, layers_of, parent=None):
    """Stages placed for one scenario: main, then every zone whose StageObjInfo row is in an active layer
    (createLocalStageDataHolder); each stage contributes common + its scenario layers. -> {stage: [layer names]}
    `parent`, if given, is filled with {zone: stage that places it}."""
    out, todo = {}, [(main, None)]
    while todo:
        st, up = todo.pop()
        if st in out:
            continue
        if parent is not None and up:
            parent[st] = up
        m = mask_of(st)
        out[st] = [LAYERS[i] for i in range(len(LAYERS)) if m >> i & 1]
        lay = layers_of(st)
        for l in out[st]:
            todo += [(z, st) for z in lay.get(l, {}).get("zones", [])]
    return out

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
    scenarios = []
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
            if SCENARIO:
                sd = {n.lower(): b for n, b in rarc_files(scen.read_bytes())}["scenariodata.bcsv"]
                layers_of = lambda st: arc_layers(stage / f"{st}.arc", cache)["layers"] if (stage / f"{st}.arc").exists() else {}
                for no, mask_of in scenario_masks(sd).items():
                    up = {}
                    act = active_stages(gname, mask_of, layers_of, up)
                    s_parts, s_objs, s_unres, per = [], set(), set(), {}
                    for st, lays in act.items():
                        zp = stage / f"{st}.arc"
                        if not zp.exists():
                            s_unres.add(st)
                            continue
                        al = arc_layers(zp, cache)
                        names = set(al["child"]) | {n for l in lays for n in al["layers"].get(l, {}).get("names", [])}
                        arcs, unknown = resolve(sorted(names))
                        s_parts.append(arc_stats(zp, cache))
                        s_objs |= lod(arcs)
                        s_unres |= unknown
                        per[st] = (zp, lod(arcs), {n for n in names if n in planets})
                    s_parts += [arc_stats(objects[n], cache) for n in sorted(s_objs)]
                    r = row(gname, s_parts)
                    # floor for any zone split: main stage + one zone (+ the stages that place it), shared archives once
                    def chain_size(z):
                        chain = [z]
                        while chain[-1] in up:
                            chain.append(up[chain[-1]])
                        chain = [c for c in dict.fromkeys(chain + [gname]) if c in per]
                        objs = set().union(*(per[c][1] for c in chain))
                        return row(z, [arc_stats(per[c][0], cache) for c in chain] +
                                   [arc_stats(objects[n], cache) for n in sorted(objs)])["ps2_total"]
                    pairs = sorted(((chain_size(z), z) for z in per), reverse=True)
                    # streaming with distant zones kept visible: zone chain in full + every other stage's planets at
                    # their lowest LOD archive, no actors (actors beyond their far clip are not drawn, ClippingJudge.cpp:39-41)
                    # ponytail: no visibility data used; actors visible from afar (bosses) would need their own model
                    low = lambda n: next(a for a in (n + "Low", n + "Middle", n) if a.lower() in objects)
                    def stream_size(z):
                        chain = [z]
                        while chain[-1] in up:
                            chain.append(up[chain[-1]])
                        chain = [c for c in dict.fromkeys(chain + [gname]) if c in per]
                        objs = set().union(*(per[c][1] for c in chain))
                        objs |= {low(n).lower() for c in per if c not in chain for n in per[c][2] if any(
                                 (n + x).lower() in objects for x in ("Low", "Middle", ""))}
                        return row(z, [arc_stats(per[c][0], cache) for c in per] +
                                   [arc_stats(objects[n], cache) for n in sorted(objs)])["ps2_total"]
                    streams = sorted(((stream_size(z), z) for z in per), reverse=True)
                    scenarios.append(dict(galaxy=r.pop("name"), scenario=no, **r, zones=len(act), unresolved=len(s_unres), unresolved_names=" ".join(sorted(s_unres)),
                                          zone_list=" ".join(sorted(act)), main_only_ps2=chain_size(gname) if gname in per else 0,
                                          worst_zone=pairs[0][1], worst_zone_ps2=pairs[0][0],
                                          stream_worst_zone=streams[0][1], stream_ps2=streams[0][0]))
    finally:
        cache_file.write_text(json.dumps(cache))
    for fname, rows in (("budget.csv", galaxies), ("zones.csv", zones)) + ((("scenarios.csv", scenarios),) if SCENARIO else ()):
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
    # scenario masks: Gateway-like data, zone value 2 in scenario 2 -> common + LayerB
    lay = {"G": {"common": {"zones": ["Z1"]}, "layera": {"zones": ["Z2"]}, "layerb": {"zones": ["Z3"]}},
           "Z1": {"layerb": {"zones": ["Z4"]}}}
    m = {"G": [1, 2], "Z1": [1, 2], "Z3": [1, 2], "Z4": [0, 2]}
    mask = lambda no: (lambda st: 1 | (m.get(st, [0, 0])[no - 1] << 1))
    act = active_stages("G", mask(2), lambda st: lay.get(st, {}))
    assert act == {"G": ["common", "layerb"], "Z1": ["common", "layerb"], "Z3": ["common", "layerb"],
                   "Z4": ["common", "layerb"]}, act
    up = {}
    assert set(active_stages("G", mask(1), lambda st: lay.get(st, {}), up)) == {"G", "Z1", "Z2"}
    assert up == {"Z1": "G", "Z2": "G"}, up
    up = {}
    active_stages("G", mask(2), lambda st: lay.get(st, {}), up)
    assert up["Z4"] == "Z1", up
    # scenario_masks on a real BCSV: ScenarioNo [1, 2], column G [1, 2]; missing column -> common only
    fields = b"".join(struct.pack(">IIHBB", jmap_hash(f), 0xFFFFFFFF, o, 0, 0) for f, o in (("ScenarioNo", 0), ("G", 4)))
    sd = struct.pack(">4I", 2, 2, 16 + 24, 8) + fields + struct.pack(">4I", 1, 1, 2, 2)
    sm = scenario_masks(sd)
    assert (sm[1]("G"), sm[2]("G"), sm[2]("Nope")) == (0b11, 0b101, 0b1), (sm[1]("G"), sm[2]("G"), sm[2]("Nope"))
    # SHP1: 1 shape, POS as INDEX16, one strip of 4 vertices with indices 0 1 0 2 (3 unique)
    from geom import shp1
    blk = bytearray(0xC0)
    struct.pack_into(">H", blk, 8, 1)
    struct.pack_into(">5I", blk, 0x0C, 0x40, 0x68, 0, 0x70, 0)
    struct.pack_into(">I", blk, 0x20, 0x90); struct.pack_into(">I", blk, 0x28, 0xB0)
    struct.pack_into(">HHHH", blk, 0x40, 0, 1, 0, 0)          # groups=1, desc_off=0, draw_idx=0
    struct.pack_into(">4I", blk, 0x70, 9, 3, 0xFF, 0)         # GX_VA_POS INDEX16, NULL
    blk[0x90:0x9B] = bytes([0x98, 0, 4]) + struct.pack(">4H", 0, 1, 0, 2)
    struct.pack_into(">II", blk, 0xB0, 11, 0)
    blk = bytes(blk)
    assert shp1(blk, 0)[:2] == (4, 4 * 6), shp1(blk, 0)
    assert shp1(blk, 0, 8)[1] == 3 * 6 + 16 + 4, shp1(blk, 0, 8)      # one batch
    assert shp1(blk, 0, 2)[1] == (2 * 6 + 16) + (6 + 16) + 4, shp1(blk, 0, 2)  # cut before index 2
    print("selftest ok")

if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        VU_POOL = next((int(a.split("=")[1]) for a in sys.argv if a.startswith("--vu-index=")), None)
        args = [a for a in sys.argv[1:] if a not in ("--levers", "--scenario") and not a.startswith("--vu-index=")]
        LEVERS = "--levers" in sys.argv
        SCENARIO = "--scenario" in sys.argv
        main(args[0], args[1] if len(args) > 1 else "out")
