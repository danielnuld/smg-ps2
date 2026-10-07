"""Before/after renders of simplified J3D models: flat-shaded, untextured, painter's algorithm (PIL + numpy).

usage: python tools/render.py data/fs/files docs/renders Model1 Model2 ...   (ObjectData/<Model>.arc)
One PNG per model, one panel per R. Positions are drawn as stored (no joint transforms): fine for planets,
which are static; skinned models would show parts at their joint origin.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
from budget import rarc_files
from geom import model

RATIOS = (1.0, 0.75, 0.5, 0.35)
SIZE = 480
VIEW = np.array([0.6, 0.5, 0.62])          # direction from the model to the camera
LIGHT = np.array([0.3, 0.8, 0.5])


def triangles(b, ratio):
    got = []
    model(b, ratio, None, got)
    out = [np.asarray(pos, dtype=np.float32).reshape(-1, 3)[np.asarray(tri, dtype=np.int64).reshape(-1, 3)]
           for pos, tri in got if tri]
    return np.concatenate(out) if out else np.zeros((0, 3, 3), np.float32)


def basis():
    f = VIEW / np.linalg.norm(VIEW)
    r = np.cross([0.0, 1.0, 0.0], f); r /= np.linalg.norm(r)
    return r, np.cross(f, r), f


def panel(tris, lo, hi, label, zoom=1.0):
    img = Image.new("RGB", (SIZE, SIZE), (24, 26, 32))
    d = ImageDraw.Draw(img)
    r, u, f = basis()
    c, scale = (lo + hi) / 2, zoom * SIZE * 0.45 / (np.linalg.norm(hi - lo) / 2 or 1)
    p = tris - c
    sx, sy, depth = p @ r * scale + SIZE / 2, SIZE / 2 - p @ u * scale, p @ f
    n = np.cross(tris[:, 1] - tris[:, 0], tris[:, 2] - tris[:, 0])
    n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-12
    shade = 0.25 + 0.75 * np.abs(n @ (LIGHT / np.linalg.norm(LIGHT)))  # two-sided: winding varies per strip
    vis = ((sx > -SIZE) & (sx < 2 * SIZE) & (sy > -SIZE) & (sy < 2 * SIZE)).all(axis=1)
    for i in np.argsort(depth.mean(axis=1)):                             # far to near
        if not vis[i]:
            continue
        v = int(40 + 200 * shade[i])
        d.polygon([(sx[i, k], sy[i, k]) for k in range(3)], fill=(v, v, int(v * 0.95)))
    d.text((8, 8), label, fill=(255, 255, 255))
    return img


def main(fs, outdir, names):
    outdir = Path(outdir); outdir.mkdir(parents=True, exist_ok=True)
    for name in names:
        arc = Path(fs) / "ObjectData" / f"{name}.arc"
        b = next(d for n, d in rarc_files(arc.read_bytes()) if n.lower().endswith((".bdl", ".bmd")) and d[:4] == b"J3D2")
        meshes = [(r, triangles(b, r)) for r in RATIOS]
        allv = meshes[0][1].reshape(-1, 3)
        lo, hi = allv.min(axis=0), allv.max(axis=0)
        sheet = Image.new("RGB", (SIZE * len(meshes), SIZE * 2))
        for i, (r, t) in enumerate(meshes):  # row 1: whole model; row 2: 4x zoom on the bounding-box centre
            sheet.paste(panel(t, lo, hi, f"{name}  R={r}  {len(t):,} tris"), (i * SIZE, 0))
            sheet.paste(panel(t, lo, hi, f"4x  R={r}", 4.0), (i * SIZE, SIZE))
        sheet.save(outdir / f"{name}.png")
        print(name, [len(t) for _, t in meshes])


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3:])
