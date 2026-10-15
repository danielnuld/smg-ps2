"""Light data (ObjectData/LightData.arc) -> the LGHT chunk, after Petari LightFunction / LightDataHolder /
LightZoneDataHolder: LightData.bcsv rows are area lights (AreaLightName, Interpolate, and for each actor light type
Player / Strong / Weak / Planet: Light0 / Light1 {Color RGBA, Pos XYZ, FollowCamera}, Alpha2, Ambient RGBA);
Light<Zone>.bcsv maps a zone's LightIDs (LightCtrlCube arg0; -1 the zone's default) to area light names. A name not
found is the first row (findAreaLight); the stage's default is zone 0's LightID -1 (getDefaultStageAreaLightName).

LGHT: u32 narea, u32 default area, u32 nzone, u32 fallback area (a LightID its zone does not list: デフォルト,
      getDefaultAreaLightName); narea x {s32 interpolate, 12 pad, 4 x TYPE (Player, Strong, Weak, Planet)};
      nzone x {u32 n, n x {s32 LightID, u32 area}} (the scenario's zones, ZONE order).
TYPE: 2 x {u8 rgba[4], f32 pos[3], u32 follow camera}, u8 ambient rgba[4], u32 alpha2 (48 B).

usage: python tools/light.py data/fs/files Galaxy   (dump)
       python tools/light.py --selftest
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import budget as B  # noqa: E402

TYPES = ("Player", "Strong", "Weak", "Planet")


def fcol(b, field):
    """A BCSV float column (budget.bcsv_column reads ints and strings only)."""
    n, nf, data, esize = struct.unpack_from(">4I", b, 0)
    want = B.jmap_hash(field)
    for i in range(nf):
        fh, mask, foff, shift, ftype = struct.unpack_from(">IIHBB", b, 16 + i * 12)
        if fh == want and ftype == 2:
            return [struct.unpack_from(">f", b, data + e * esize + foff)[0] for e in range(n)]
    return []


def files(fs):
    return {k.lower(): v for k, v in B.rarc_files((Path(fs) / "ObjectData" / "LightData.arc").read_bytes())}


def areas(ld):
    """LightData.bcsv -> [{name, interpolate, types: {type: dict(lights=[(rgba, pos, follow)] x 2, amb, alpha2)}}]"""
    names = B.bcsv_column(ld, "AreaLightName")
    n = len(names)
    ints = lambda f: B.bcsv_column(ld, f) or [0] * n
    out = []
    cache = {}
    get = lambda f: cache.setdefault(f, ints(f) if not f.endswith(("PosX", "PosY", "PosZ")) else fcol(ld, f) or [0.0] * n)
    for i in range(n):
        it = get("Interpolate")[i]
        types = {}
        for t in TYPES:
            lights = []
            for l in ("Light0", "Light1"):
                rgba = tuple(get(f"{t}{l}Color{c}")[i] & 255 for c in "RGBA")
                pos = tuple(get(f"{t}{l}Pos{c}")[i] for c in "XYZ")
                lights.append((rgba, pos, get(f"{t}{l}FollowCamera")[i] & 1))
            types[t] = dict(lights=lights, amb=tuple(get(f"{t}Ambient{c}")[i] & 255 for c in "RGBA"),
                            alpha2=get(f"{t}Alpha2")[i] & 255)
        out.append(dict(name=names[i], interpolate=it - 2**32 if it >= 2**31 else it, types=types))
    return out


def zone_table(fl, zone):
    """Light<Zone>.bcsv -> [(LightID, area light name)], [] if the zone has none."""
    b = fl.get(f"light{zone.lower()}.bcsv")
    if not b:
        return []
    ids = [x - 2**32 if x >= 2**31 else x for x in B.bcsv_column(b, "LightID")]
    return list(zip(ids, B.bcsv_column(b, "AreaLightName")))


def chunk_payload(fs, zones):
    """LGHT payload for the scenario's zones (names, ZONE order): the areas they name, the stage's default first
    referenced."""
    fl = files(fs)
    rows = areas(fl["lightdata.bcsv"])
    index = {r["name"]: i for i, r in reversed(list(enumerate(rows)))}  # the first row of a name (findAreaLight)
    used, tables = [], []

    def area(name):
        k = index.get(name, 0)
        if k not in used:
            used.append(k)
        return used.index(k)
    zt = [zone_table(fl, z) for z in zones]
    d = dict(zt[0]).get(-1) if zt else None
    default = area(d if d is not None else "デフォルト")
    fallback = area("デフォルト")
    for t in zt:
        tables.append([(lid, area(name)) for lid, name in t])
    out = bytearray(struct.pack("<IIII", len(used), default, len(tables), fallback))
    for k in used:
        r = rows[k]
        out += struct.pack("<i12x", r["interpolate"])
        for t in TYPES:
            ty = r["types"][t]
            for rgba, pos, follow in ty["lights"]:
                out += struct.pack("<4B3fI", *rgba, *pos, follow)
            out += struct.pack("<4BI", *ty["amb"], ty["alpha2"])
    for t in tables:
        out += struct.pack("<I", len(t)) + b"".join(struct.pack("<iI", lid, a) for lid, a in t)
    return bytes(out), [rows[k]["name"] for k in used]


def selftest():
    fs = Path(__file__).resolve().parent.parent / "data/fs/files"
    if not (fs / "ObjectData" / "LightData.arc").exists():
        print("selftest skipped (no data)")
        return
    pay, names = chunk_payload(fs, ["EggStarGalaxy", "DiskGardenZone"])
    narea, default, nzone = struct.unpack_from("<III", pay, 0)
    assert narea == len(names) and nzone == 2 and names[default] == "[デフォルト]エッグプラネット", names[default]
    rgba = struct.unpack_from("<4B3fI", pay, 16 + default * 208 + 16 + 3 * 48)  # Planet light 0
    assert rgba[:3] == (255, 255, 255) and rgba[4] == 700000.0 and rgba[7] == 0, rgba
    assert len(pay) == 16 + 208 * narea + sum(4 + 8 * n for n in [21, len(zone_table(files(fs), "DiskGardenZone"))])
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        fl = files(sys.argv[1])
        for lid, name in zone_table(fl, sys.argv[2]):
            print(lid, name)
