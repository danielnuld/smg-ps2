"""GS memory the textures of each scenario need with streaming: resident + common + the worst zone chain in full +
every other stage low, as the engine uploads them (render.c: PSMT4 rows of 128 pixels per level (base + nmip mips)
+ a 64-word CLUT, each rounded to 64-word blocks).

usage: python tools/vram_need.py out/packs [free KB, default 2304: 4 MB minus the 640 x 224 frame buffers, Z and the bloom buffers]
"""
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import slice as SL


BLK4 = [[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15], [16, 18, 24, 26], [17, 19, 25, 27],
        [20, 22, 28, 30], [21, 23, 29, 31]]
BLK8 = [[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15], [16, 17, 20, 21, 24, 25, 28, 29],
        [18, 19, 22, 23, 26, 27, 30, 31]]


def footprint(t8, w, h, bw):
    """Blocks a w x h PSMT4 / PSMT8 image spans in a buffer bw pixels wide (render.c footprint: the GS swizzles the
    blocks of a page, so strips shorter than a page reach further than their size)."""
    bkw, pw, ph = (16, 128, 64) if t8 else (32, 128, 128)
    top = 0
    for y in range(0, h, 16):
        for x in range(0, w, bkw):
            b = (BLK8 if t8 else BLK4)[(y % ph) // 16][(x % pw) // bkw]
            top = max(top, ((y // ph) * (bw // pw) + x // pw) * 32 + b + 1)
    return top


def tex_words(blob):
    t = SL.read_chunks(blob).get(b"TEXT")
    if not t:
        return 0
    n = struct.unpack_from("<I", t, 0)[0]
    words = 0
    for i in range(n):
        w, h, psm = struct.unpack_from("<HHB", t, 16 + 32 * i)
        nmip = t[16 + 32 * i + 22]
        if psm == 0x13:  # PSMT8 (skies): a 4-block CLUT, then rows of 128 or 256 pixels
            words += 64 * (4 + footprint(True, w, h, 256 if w > 128 else 128))
        else:  # PSMT4: each level in a 128-pixel buffer, then a 1-block CLUT
            words += 64 * (1 + sum(footprint(False, w >> k, h >> k, 128) for k in range(nmip + 1)))
    return words


def main(packs, free_kb=2304):
    packs = Path(packs)
    base = tex_words((packs / "resident.bin").read_bytes())
    rows = []
    for d in sorted(p for p in packs.glob("*/*") if (p / "scene.bin").exists()):
        sc = SL.read_chunks((d / "scene.bin").read_bytes())
        nz = struct.unpack_from("<I", sc[b"ZONE"], 0)[0]
        names = [sc[b"ZONE"][4 + 56 * z:4 + 56 * z + 32].split(b"\0")[0].decode() for z in range(nz)]
        parent = struct.unpack_from(f"<{nz}i", sc[b"ZPAR"], 4)
        full = {n: tex_words((d / f"{n}.bin").read_bytes()) for n in names}
        low = {n: tex_words((d / f"{n}.low.bin").read_bytes()) for n in names}
        common = tex_words((d / "common.bin").read_bytes())
        worst = (0, "")
        for z in range(nz):
            chain, c = set(), z
            while c >= 0:
                chain.add(c)
                c = parent[c]
            chain.add(0)
            need = base + common + sum(full[names[i]] if i in chain else low[names[i]] for i in range(nz))
            worst = max(worst, (need, names[z]))
        kb = worst[0] * 4 / 1024
        rows.append(dict(galaxy=d.parent.name, scenario=d.name, vram_kb=round(kb), worst_stage=worst[1],
                         fits=int(kb <= free_kb)))
    rows.sort(key=lambda r: -r["vram_kb"])
    with open(packs / "vram.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
    print(f"{sum(r['fits'] for r in rows)} / {len(rows)} fit {free_kb} KB; worst {rows[0]['galaxy']} {rows[0]['scenario']} "
          f"{rows[0]['vram_kb']} KB ({rows[0]['worst_stage']}); median {rows[len(rows) // 2]['vram_kb']} KB")


def selftest():
    assert footprint(False, 128, 32, 128) == 12 and footprint(False, 128, 128, 128) == 32
    assert footprint(False, 64, 64, 128) == 8 and footprint(True, 256, 32, 256) == 48
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
        sys.exit()
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 2304)
