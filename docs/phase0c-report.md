# Phase 0c report (in progress)

## Mesh simplification (tasks 1.1-1.2)

Simplifier: meshoptimizer v1.3 (`bin/meshopt.dll` via ctypes, `tools/simplify.py`). Per J3D matrix group: display-list
primitives -> triangles (`geom.triangles`), positions from VTX1, `meshopt_simplify` to ~R of the triangles, then
`optimizeVertexCacheStrip` + `stripify` with restart markers (free on the GS: XYZ3, ps2tek:1946-1951).

First try used degenerate joins and no cache ordering: 2.1-2.7 emitted vertices per triangle against Nintendo's 1.55,
so R = 0.75 came out **bigger** than the original. With ordering + restarts it is 1.53 per triangle.

GhostShipCavePlanet.bdl (the heaviest planet of the worst scenario), 38 692 triangles, 17 matrix groups:

| R | Emitted vertices | De-indexed MB | `--vu-index=255` MB |
|---|---|---|---|
| 1 (original strips) | 60 073 | 1.15 | 1.00 |
| 0.99 (re-stripped only) | 58 736 | 1.12 | 0.77 |
| 0.75 | 46 666 | 0.89 | 0.64 |
| 0.5 | 36 644 | 0.70 | 0.54 |
| 0.35 | 31 154 | 0.59 | 0.48 |

Re-stripping alone saves 23 % in VU1 mode (better batch locality), before removing any triangle. The simplifier
stops short of its target on meshes with many UV/normal seams: at R = 0.5 it keeps 58 % of the triangles
(`meshopt_SimplifyPermissive` would go further at a texture-quality cost; not used).
