"""List every zone whose chain can not stream in (main.c target_set / load_packs' "never fits" line, offline).

usage: python tools/zone_fit.py out/packs [room_kb]
For each <Galaxy>/<n>/scene.bin: each zone's chain (ZPAR) in full, every other stage low, plus scene.bin and
common.bin, each pack rounded to whole sectors (main.c PACK_ALLOC), against slice_all.room (ROOM less the galaxy's menu
packs and letter.bin), or room_kb less those (ROOM measured on the PS2: the "packs: ... room N KB" line plus scene.bin
and common.bin and the menu packs, 8962 KB with a 13824 KB region). Prints the zones over it and the tightest.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import slice_all

SECTOR = 2048


def chunk(blob, tag):
    n, p = struct.unpack_from("<I", blob, 8)[0], 16
    for _ in range(n):
        sz = struct.unpack_from("<I", blob, p + 4)[0]
        if blob[p:p + 4] == tag:
            return blob[p + 16:p + 16 + sz]
        p += 16 + sz
    raise KeyError(tag)


def alloc(p):
    return -(-p.stat().st_size // SECTOR) * SECTOR if p.exists() else 0


def scenario(out, d, room_kb):
    scene = (d / "scene.bin").read_bytes()
    zn, zp = chunk(scene, b"ZONE"), chunk(scene, b"ZPAR")
    n = struct.unpack_from("<I", zn)[0]
    names = [zn[4 + 56 * z:4 + 56 * z + 32].split(b"\0")[0].decode() for z in range(n)]
    parent = struct.unpack_from(f"<{n}i", zp, 4)
    room = int(slice_all.room(out, d.parent.name) + (room_kb * 1024 - slice_all.ROOM if room_kb else 0))
    base = alloc(d / "scene.bin") + alloc(d / "common.bin")
    for z in range(n):
        chain, k = {0}, z
        while 0 <= k < n:
            chain.add(k)
            k = parent[k]
        cost = base + sum(alloc(d / f"{names[s]}.bin" if s in chain else d / f"{names[s]}.low.bin") for s in range(n))
        yield names[z], cost, room


if __name__ == "__main__":
    out = Path(sys.argv[1])
    room_kb = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    rows = [(f"{d.parent.name}/{d.name}", z, c, r - c, r) for d in sorted(out.glob("*/*/")) if (d / "scene.bin").exists()
            for z, c, r in scenario(out, d, room_kb)]
    over = [r for r in rows if r[3] < 0]
    for s, z, c, m, r in over:
        print(f"never fits: {s} {z}: {c >> 10} KB, room {r >> 10} KB")
    print(f"{len(rows)} zones in {len({r[0] for r in rows})} scenarios, {len(over)} over their room; tightest:")
    for s, z, c, m, r in sorted(rows, key=lambda r: r[3])[:8]:
        print(f"  {s} {z}: {c >> 10} KB ({m >> 10} KB left)")
