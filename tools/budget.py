"""Per-galaxy memory budget: original Wii asset sizes vs. estimated PS2-converted sizes.

usage: python tools/budget.py data/fs [out_dir]    (data/fs = extracted disc filesystem)
       python tools/budget.py --selftest
"""
import csv, json, struct, sys
from pathlib import Path

def u16(b, o): return struct.unpack_from(">H", b, o)[0]
def u32(b, o): return struct.unpack_from(">I", b, o)[0]

# ---------- Yaz0 ----------
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

# ---------- RARC ----------
def rarc_files(arc):
    """Yield (name, bytes) for every file in a (decompressed) RARC."""
    arc = yaz0(arc)
    if arc[:4] != b"RARC":
        return
    data_off = u32(arc, 0x0C) + 0x20
    info = 0x20
    n_entries, entry_off = u32(arc, info + 0x08), u32(arc, info + 0x0C) + 0x20
    str_off = u32(arc, info + 0x14) + 0x20
    for i in range(n_entries):
        e = entry_off + i * 0x14
        type_name = u32(arc, e + 4)
        if (type_name >> 24) & 0x02:  # directory
            continue
        name_off = type_name & 0xFFFFFF
        name = arc[str_off + name_off: arc.index(b"\0", str_off + name_off)].decode("shift_jis", "replace")
        off, size = u32(arc, e + 8) + data_off, u32(arc, e + 12)
        yield name, yaz0(arc[off:off + size])

# ---------- textures ----------
# GX format -> (bits per pixel, block w, block h)
GX_FMT = {0: (4, 8, 8), 1: (8, 8, 4), 2: (8, 8, 4), 3: (16, 4, 4), 4: (16, 4, 4), 5: (16, 4, 4),
          6: (32, 4, 4), 8: (4, 8, 8), 9: (8, 8, 4), 10: (16, 4, 4), 14: (4, 8, 8)}
PS2_4BPP = {0, 2, 8, 14}  # I4, IA4, C4, CMPR -> CLUT4; everything else -> CLUT8
PS2_MAX_DIM = 256

def gx_tex_size(fmt, w, h, mips):
    bpp, bw, bh = GX_FMT.get(fmt, (32, 4, 4))
    total = 0
    for _ in range(max(mips, 1)):
        total += (-(-w // bw) * bw) * (-(-h // bh) * bh) * bpp // 8
        w, h = max(w // 2, 1), max(h // 2, 1)
    return total

def ps2_tex_size(fmt, w, h, mips):
    bpp = 4 if fmt in PS2_4BPP else 8
    mips = max(mips, 1)
    while (w > PS2_MAX_DIM or h > PS2_MAX_DIM) and mips > 1:  # drop top mip levels
        w, h, mips = max(w // 2, 1), max(h // 2, 1), mips - 1
    while w > PS2_MAX_DIM or h > PS2_MAX_DIM:                 # no mips left: downscale
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

# ---------- J3D (bmd/bdl) ----------
GX_COMP_SIZE = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}  # u8 s8 u16 s16 f32
GX_CLR_SIZE = {0: 2, 1: 3, 2: 4, 3: 2, 4: 3, 5: 4}

def j3d_sizes(b):
    """-> dict tex_gx, tex_ps2, geo_gx, geo_ps2, other."""
    r = dict(tex_gx=0, tex_ps2=0, geo_gx=0, geo_ps2=0, other=0)
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
            r["geo_gx"] += size
            r["geo_ps2"] += vtx1_ps2(b, o, size)
        elif tag == b"SHP1":
            # ponytail: indices kept as-is (EE de-indexes at draw time); pre-expanded strips could be 2-3x
            r["geo_gx"] += size
            r["geo_ps2"] += size
        else:
            r["other"] += size
        o += size
    return r

def vtx1_ps2(b, o, size):
    """VTX1 arrays re-encoded with 16-bit components (colors stay 4 bytes)."""
    fmts, f = {}, o + u32(b, o + 8)
    while u32(b, f) != 0xFF:
        fmts[u32(b, f)] = (u32(b, f + 4), u32(b, f + 8))
        f += 0x10
    offs = [u32(b, o + 0x0C + 4 * i) for i in range(13)]
    # array index -> GX attribute: pos 9, nrm 10, nbt, clr0 11, clr1 12, tex0-7 13-20
    attrs = [9, 10, None, 11, 12] + list(range(13, 21))
    live = sorted(x for x in offs if x)
    total = 0
    for i, off in enumerate(offs):
        if not off or attrs[i] not in fmts:
            continue
        nxt = next((x for x in live if x > off), size)
        length = nxt - off
        cnt, typ = fmts[attrs[i]]
        if attrs[i] in (11, 12):
            total += length // GX_CLR_SIZE.get(typ, 4) * 4
            continue
        comps = 2 + cnt if attrs[i] == 9 else (3 if cnt == 0 else 9) if attrs[i] == 10 else 1 + cnt
        elem = comps * GX_COMP_SIZE.get(typ, 4)
        total += length // elem * comps * 2
    return total

def file_sizes(name, b):
    n = name.lower()
    if n.endswith((".bmd", ".bdl")) and b[:4] == b"J3D2":
        return j3d_sizes(b)
    if n.endswith(".bti"):
        fmt, w, h, mips, _ = next(bti_headers(b, 0, 1))
        return dict(tex_gx=len(b), tex_ps2=ps2_tex_size(fmt, w, h, mips), geo_gx=0, geo_ps2=0, other=0)
    return dict(tex_gx=0, tex_ps2=0, geo_gx=0, geo_ps2=0, other=len(b))

# ---------- BCSV (JMap) ----------
def jmap_hash(s):
    h = 0
    for c in s.encode("shift_jis"):
        h = (h * 31 + c) & 0xFFFFFFFF
    return h

def bcsv_column(b, field):
    """Values of string column `field`, or [] if absent / not a BCSV."""
    if len(b) < 16:
        return []
    n, nf, data, esize = struct.unpack_from(">4I", b, 0)
    if not (0 < nf < 256 and 16 + nf * 12 <= data <= len(b) and data + n * esize <= len(b)):
        return []
    want = jmap_hash(field)
    for i in range(nf):
        fh, _mask, foff, _shift, ftype = struct.unpack_from(">IIHBB", b, 16 + i * 12)
        if fh != want:
            continue
        strings = data + n * esize
        out = []
        for e in range(n):
            base = data + e * esize + foff
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

# ---------- disc walk ----------
KEYS = ("tex_gx", "tex_ps2", "geo_gx", "geo_ps2", "other")

def arc_stats(path, cache):
    key = f"{path}:{path.stat().st_mtime_ns}"
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
    r["ps2_total"] = r["tex_ps2"] + r["geo_ps2"] + r["other"]
    return dict(name=label, **r)

def main(fs, out):
    fs, out = Path(fs), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    cache_file = out / "cache.json"
    cache = json.loads(cache_file.read_text()) if cache_file.exists() else {}
    stage, objdir = fs / "StageData", fs / "ObjectData"
    objects = {p.stem.lower(): p for p in objdir.glob("*.arc")}
    galaxies, zones = [], []
    try:
        for scen in sorted(stage.glob("*Scenario.arc")):
            gname = scen.name[: -len("Scenario.arc")]
            print(gname, flush=True)
            zone_names = [gname] + [z for z in arc_stats(scen, cache)["zones"] if z != gname]
            g_parts, g_objs, unresolved = [], set(), set()
            for z in zone_names:
                zp = stage / f"{z}.arc"
                if not zp.exists():
                    unresolved.add(z)
                    continue
                zs = arc_stats(zp, cache)
                objs = {n.lower() for n in zs["names"]}
                z_parts = [zs] + [arc_stats(objects[n], cache) for n in sorted(objs & objects.keys())]
                zones.append(dict(galaxy=gname, **row(z, z_parts), unresolved=len(objs - objects.keys())))
                g_parts.append(zs)
                g_objs |= objs & objects.keys()
                unresolved |= objs - objects.keys()
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
        main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "out")
