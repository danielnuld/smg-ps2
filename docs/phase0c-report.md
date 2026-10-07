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

## `--simplify=R` in the budget (task 1.3)

`budget.py ... --simplify=R` passes R and the VTX1 positions to `geom.shp1`; the cache key carries R. Check: a run
from an empty cache with `--levers --scenario --vu-index=255 --simplify=1` writes `budget.csv`, `zones.csv` and
`scenarios.csv` byte-identical to `out/vu255` (`cmp`, 39 s).

## Sweep (task 1.4) — `budget.py data/fs/files out/sR --levers --scenario --vu-index=255 --simplify=R`

Per scenario the better of whole and streaming (0b rule). Margin before audio and heap = room − scenario. The audio
column subtracts the 1.88 MB Wii audio solid heap as a **bound** (task 2.4 replaces it); heap is still not counted.

| R | Resident (geo) | Room | Worst scenario | Margin | − 1.88 audio | Negative |
|---|---|---|---|---|---|---|
| 1 (0b, original strips) | 14.44 (3.31) | 9.85 | OceanPhantomCave 1 | +0.76 | −1.12 | 6 / 136 |
| 0.99 (re-stripped only) | 13.95 (2.82) | 10.34 | OceanPhantomCave 1 | +2.13 | +0.25 | 0 / 136 |
| 0.75 | 13.45 (2.32) | 10.84 | OceanPhantomCave 1 | +3.29 | +1.41 | 0 / 136 |
| 0.5 | 12.96 (1.83) | 11.33 | OceanPhantomCave 1 | +4.38 | +2.50 | 0 / 136 |
| 0.35 | 12.70 (1.57) | 11.59 | OceanPhantomCave 1 | +5.02 | +3.14 | 0 / 136 |

Re-stripping alone (R = 0.99, no visible change) is worth 1.37 MB on the worst scenario; each further step of
simplification gives about 1 MB. Whatever R is chosen, the heap now has 1.4-3.1 MB to live in (with the audio bound).
Which R is acceptable is decided by the renders (task 1.5), not by the budget.

## Renders (task 1.5) — `python tools/render.py data/fs/files docs/renders <models>`

Flat-shaded, untextured, painter's algorithm (PIL + numpy); row 1 the whole model, row 2 a 4× zoom on its centre.
Models: the three planets with the most PS2 geometry (PeachCastleGardenPlanet 1.93 MB, DarkHopperPlanetC 1.62,
TrialBallPlanet 1.57, from `out/vu255/cache.json`) plus GhostShipCavePlanet (worst scenario).

![PeachCastleGarden](renders/PeachCastleGardenPlanet.png)
![DarkHopperC](renders/DarkHopperPlanetC.png)
![TrialBall](renders/TrialBallPlanet.png)
![GhostShipCave](renders/GhostShipCavePlanet.png)

Triangles kept (the simplifier stops early on seams): Peach 56 483 → 47 112 / 40 654 / 37 258 at R 0.75 / 0.5 / 0.35;
DarkHopperC 50 610 → 41 860 / 35 164 / 31 843; TrialBall 41 190 → 34 549 / 33 167 / 32 574;
GhostShipCave 38 692 → 29 558 / 22 478 / 18 632.

| Model | R = 0.75 | R = 0.5 | R = 0.35 |
|---|---|---|---|
| GhostShipCave | same | same | same |
| PeachCastleGarden | same | small cracks along pond edges | broken: long slivers, holes |
| DarkHopperC | a few new notches | visible spikes and notches | small detail gone |
| TrialBall | **round holes turn into triangles, curved edges facet** | worse | worse |

**Judgement:** no uniform R below 1 is clean. 0.75 already damages curved, gameplay-relevant shapes (TrialBall is a
rolling-ball course: collision `.kcl` is not simplified, so a visibly triangular hole would not match the round
collision hole). Only re-stripping (R = 0.99, no visible change) is free; it alone gives the worst scenario +2.13 MB
before audio and heap (+0.25 with the 1.88 MB audio bound).

The next step for geometry, if needed after audio and heap, is an **error-capped** simplification (meshoptimizer's
`target_error` instead of a fixed ratio, per model) so flat terrain like GhostShipCave or Peach's slopes shrinks and
curves like TrialBall's are left alone. Not done here.
