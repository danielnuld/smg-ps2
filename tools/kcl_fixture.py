"""Write engine/test/fixtures/{planet.kcl, planet_prisms.txt} for engine/test/test_kcl.c.
planet.kcl is the slice planet's KCL after slice.kcl_to_le; each line of planet_prisms.txt holds a prism index, its
triangle's centroid and its face normal. Triangle corners from the prism: the usual KCL reconstruction
(v1 = v0 + cross(edge2, n) * h / dot(cross(edge2, n), edge3), v2 likewise with edge1)."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B
import slice as S

ROOT = Path(__file__).resolve().parent.parent
k = next(d for n, d in B.rarc_files((ROOT / "data/fs/files/ObjectData/HeavensDoorMysteriousPlanet.arc").read_bytes())
         if n.lower().endswith(".kcl"))
le = S.kcl_to_le(k)
out = ROOT / "engine/test/fixtures"
out.mkdir(parents=True, exist_ok=True)
(out / "planet.kcl").write_bytes(le)
pos, nrm, prism, _ = struct.unpack_from("<4I", le, 0)
V = lambda off, i: struct.unpack_from("<3f", le, off + 12 * i)
cross = lambda a, b: (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
dot = lambda a, b: sum(x * y for x, y in zip(a, b))
lines = []
for i in (1, 500, 3000):
    h, pi, ni, e0, e1, e2, _ = struct.unpack_from("<f6H", le, prism + 16 * i)
    v0, fn, en1, en2, en3 = V(pos, pi), V(nrm, ni), V(nrm, e0), V(nrm, e1), V(nrm, e2)
    ca, cb = cross(en1, fn), cross(en2, fn)
    v1 = [v0[j] + cb[j] * (h / dot(cb, en3)) for j in range(3)]
    v2 = [v0[j] + ca[j] * (h / dot(ca, en3)) for j in range(3)]
    c = [(v0[j] + v1[j] + v2[j]) / 3 for j in range(3)]
    lines.append(f"{i} {c[0]} {c[1]} {c[2]} {fn[0]} {fn[1]} {fn[2]}")
(out / "planet_prisms.txt").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
