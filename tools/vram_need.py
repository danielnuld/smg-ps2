"""GS memory the textures of each scenario need with streaming: resident + common + the worst zone chain in full +
every other stage low, as the engine uploads them (render.c: PSMT4 rows of 128 pixels + a 64-word CLUT, both
rounded to 64-word blocks).

usage: python tools/vram_need.py out/packs [free KB, default 720: 4 MB minus frame buffers and Z]
"""
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import slice as SL


def tex_words(blob):
    t = SL.read_chunks(blob).get(b"TEXT")
    if not t:
        return 0
    n = struct.unpack_from("<I", t, 0)[0]
    words = 0
    for i in range(n):
        w, h = struct.unpack_from("<HH", t, 16 + 32 * i)
        words += (128 * h // 8 + 63) // 64 * 64 + 64
    return words


def main(packs, free_kb=720):
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


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 720)
