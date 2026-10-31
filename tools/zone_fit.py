"""List every zone whose chain can not stream in (main.c target_set / load_packs' "never fits" line, offline).

usage: python tools/zone_fit.py out/packs [room_kb | --elf boot.elf]
--elf: the room that ELF leaves (region_kb: main.c pk_init takes the heap less 1.5 MB in 256 KB steps), exit status 1
if a zone is over it (tools/disc.py runs it on the disc's ELF).
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


def elf_end(path):
    """The end of an ELF's loaded segments (text, data, bss): the heap starts there"""
    b = Path(path).read_bytes()
    phoff, = struct.unpack_from("<I", b, 28)
    phentsize, phnum = struct.unpack_from("<HH", b, 42)
    end = 0
    for i in range(phnum):
        t, _, va, _, _, memsz, _, _ = struct.unpack_from("<8I", b, phoff + i * phentsize)
        if t == 1:
            end = max(end, va + memsz)
    return end


# main.c pk_init's region against the ELF's end, measured in PCSX2 (256 KB steps): ends of 16736-16787 KB gave 13568 KB,
# 16866 and 16936 KB 13312 KB, 17209 KB 13056 KB. HEAP_KB is the lowest constant that fits them all (30355-30433), so
# the region here is never above the PS2's
HEAP_KB = 30355
ROOM_KB, ROOM_REGION_KB = 8962, 13824  # ROOM measured with a 13824 KB region (the docstring)


def region_kb(elf):
    return (HEAP_KB - elf_end(elf) // 1024) // 256 * 256


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
        yield names[z], cost, room, z


if __name__ == "__main__":
    out = Path(sys.argv[1])
    room_kb = int(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] != "--elf" else 0
    if sys.argv[2:3] == ["--elf"]:
        reg = region_kb(sys.argv[3])
        room_kb = ROOM_KB - (ROOM_REGION_KB - reg)
        print(f"{sys.argv[3]}: ends at {elf_end(sys.argv[3]) >> 10} KB, region {reg} KB, room {room_kb} KB")
    rows = [(f"{d.parent.name}/{d.name}", z, c, r - c, r, k) for d in sorted(out.glob("*/*/")) if (d / "scene.bin").exists()
            for z, c, r, k in scenario(out, d, room_kb)]
    over = [r for r in rows if r[3] < 0]
    for s, z, c, m, r, k in over:
        print(f"{'STAGE NEVER LOADS' if k == 0 else 'never fits'}: {s} {z}: {c >> 10} KB, room {r >> 10} KB")
    print(f"{len(rows)} zones in {len({r[0] for r in rows})} scenarios, {len(over)} over their room; tightest:")
    for s, z, c, m, r, k in sorted(rows, key=lambda r: r[3])[:8]:
        print(f"  {s} {z}: {c >> 10} KB ({m >> 10} KB left)")
    if any(k == 0 for *_, k in over):  # a main stage over its room: "packs: no memory" on the PS2, the scenario can not start
        sys.exit("zone_fit: a scenario's main stage does not fit its room")
