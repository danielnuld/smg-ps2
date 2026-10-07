# Phase 1 report

## Wii reference (tasks 1.1-1.2)

HeavensDoorGalaxy scenario 2 (Gateway, purple coins), Dolphin 2609, dump 30 s after load
(`tools/dolphin/measure.ps1 HeavensDoorGalaxy 2 hd2`; checked on screen: Mysterious planet, purple-coin counter).

| Heap | Used |
|---|---|
| File cache (archives) | 3.59 MB |
| Scene heap MEM1 | 2.92 MB |
| Scene heap MEM2 (render targets) | 0.32 MB |
| **Scene heap total** | **3.24 MB**, 1 295 NameObjs |

Per class (`tools/heapclass.py`; each object charged up to the next one, so per-class bytes are approximate):

| Type | Objects | Bytes | Per object | Classes counted |
|---|---|---|---|---|
| PurpleCoin | 149 | 373.5 KB | 2 567 B | coin ×142, counter, manager, 4 circle groups, starter |
| StarPiece | 77 | 380.9 KB | 5 066 B | director pieces ×73, counter, 2 stars, director |
| Kuribo | 4 | 47.2 KB | 12 073 B | クリボー ×4 |
| Mario / player | 51 | 296.6 KB | — | hands, parts, shadows, action tables, fireballs, tornado… (matched by Petari file `Game/Player/` or name) |
| Coin | — | — | ~2 830 B | none live in this scene (only manager 10 KB, counter 6 KB); per-object value from OceanPhantomCave 1, `RailCoin` 91 objects / 257 KB |

Fixed costs that do not scale with objects: 3D particle executor 0.54 MB, pause menu 60 KB, clipping holder 40 KB,
information observer 40 KB, Yes/No layout 32 KB.

**Targets for the slice** (a sixth of the Wii, design decision 1): PurpleCoin ≤ ~430 B, StarPiece ≤ ~840 B,
Kuribo ≤ ~2 KB, Mario/player ≤ ~50 KB, scene runtime ≤ ~0.54 MB.

## Slice asset blob (tasks 2.1-2.2) — `python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin`

Format documented in the script header (little-endian chunks: TYPE, ZONE, OBJS, STRT, PATH, MODL, KCL). Contents for
Gateway 2 (stages HeavensDoorGalaxy + HeavensDoorMysteriousZone, layers common + layerb):

| Chunk | Bytes | Contents |
|---|---|---|
| OBJS | 5 376 | 67 rows: 42 PurpleCoin, 4 CirclePurpleCoinGroup, 1 PurpleRailCoin, 10 Coin, 4 Kuribo, 3 StarPiece, the planet, point + plane gravity |
| STRT | 496 | 6 Mario start points |
| PATH | 352 | 6 rails (control points) |
| ZONE | 128 | main stage + Mysterious zone (pos 14760, −10676, 6770; rot 65.6°, 70.1°, 56.6°) |
| MODL | 702 816 | planet + Coin, Kuribo, PurpleCoin, StarPiece (re-stripped R = 0.99, float positions, indexed triangles, untextured) |
| KCL | 632 480 | planet collision as on disc |
| **Blob** | **1 342 064 (1.28 MB)** | `budget.py` estimate for the same scenario: 2.66 MB (includes textures, animations, all archives) |

Not in the blob yet (by design, added when the engine needs them): textures/UVs, strips, Mario's model and
animations, the planet's other parts (house door, flowers, pipes).

## Engine skeleton (tasks 3.1-3.2)

`engine/` (C99, ps2sdk + gsKit from the WSL ps2dev at `/usr/local/ps2dev`; `engine/build.sh` builds on the WSL disk).
The blob is linked into the ELF (`src/assets.S`, `.incbin`) and read in place (`blob.c`); runtime memory comes from
one arena with every byte charged to a type (`mem.c`); text is a 3x5 sprite font (the BIOS FONTM path,
`gsKit_init_fontm`, fails in this setup). 16-bit colour and Z (24-bit colour + Z24 would take ~3.4 of the GS's 4 MB).

First light on PCSX2 (`docs/renders/slice-first-light.png`): the Mysterious planet with every placed purple coin,
coin, star piece and Kuribo in place. EE-only transform, flat shading, gsKit triangle lists:

| | |
|---|---|
| Triangles drawn | 58 052 |
| EE time per frame (PCSX2, indicative only) | 173 ms (~3 µs per triangle) |
| Runtime arena | 411 KB (render scratch 48 KB, object table 4.5 KB…) + gsKit's own queues 2.25 MB (malloc, not yet charged) |
| ELF | 1.52 MB text+data (blob 1.34 MB), arena in bss |

The EE path is an order of magnitude too slow for 16.7 ms; the renderer has to move to VU1 (design decision 5 said
"VU1 only if the EE transform dominates": it does).

## Gravity and collision (tasks 4.1-4.2)

- `engine/src/gravity.c`: point and plane (sphere range) gravity with Petari's rules: highest priority in range wins,
  equal priorities add, strength 4e6 / (r − Distant)², Inverse flips (PlanetGravity.cpp, PointGravity.cpp,
  ParallelGravity.cpp, PlanetGravityManager.cpp). The blob's GRAV chunk carries kind, priority, inverse, range,
  distant, position and up. Gateway 2: point gravity (range 8200) and a priority-1 plane gravity over the planet's
  flat top (sphere of 2300 at y = 3305).
- `engine/src/kcl.c`: KCL sphere queries transcribed from Petari (searchBlock, outCheck, KCHitSphere); the converter
  turns the KCL little-endian by walking its octree (`slice.kcl_to_le`).
- Host tests (`engine/test/run.sh`, gcc in WSL): `test_gravity` (both gravities, priority, sum, inverse) and
  `test_kcl` on the real planet KCL (face hit 10 deep on three prisms, none 100 away, none outside the box).

## Gameplay subset (tasks 4.3-4.4) — `engine/src/game.c`

Playable on PCSX2 (`docs/renders/slice-walk.png`): Mario walks with the stick (camera-relative, on the plane under
him), jumps (Cross), spins (Square), follows point and plane gravity around the planet, collides with the planet KCL,
collects coins, purple coins and star pieces; the four Kuribo chase him, are stomped or stunned by the spin, and
knock him back on contact. The scene holds **100 purple coins** (42 single + 4 circles + 1 rail), the number of
Gateway's purple-coin mission, which checks the circle (arg0 count, arg2 radius, CircleCoinGroup.cpp) and rail
(arg0 count, RailCoin.cpp) placement.

Data per object, all from one arena: Pickup 16 B (coin, purple coin, star piece), Kuribo 48 B, Player 80 B.
Mario uses Petari's constants (MarioConst.cpp gMarioConstDefault: walk 13, inertia 0.9, jump 22, jump gravity 1.8,
max drop 35, spin lift 5).

Differences from SMG (not costed in the slice):
- Mario: a box instead of the skinned model, no animation; walk only (no dash, triple/long jump, wall jump, slopes,
  slip); one collision sphere; spin = small lift + stun.
- Kuribo: no model animation, chases straight on the surface, simplified stomp/side-hit tests.
- Camera: plain follow behind and above Mario (SMG's camera system is ~12.5 k lines and uses camera areas).
- Rails: straight segments between control points (the game uses Bezier rails).
- No sound, particles, shadows, HUD layouts, star bits shooting, pause menu.

## VU1 renderer (follow-up to 3.2) — `engine/src/render.c`, `engine/src/vu1_flat.vsm`

The blob gains a VUMD chunk (format v2): every model pre-split into batches of 120 vertices (40 triangles), position
and colour as VU-ready quadwords. Per frame the EE only builds a DMA chain of REF tags (matrix per instance, then
UNPACK + MSCAL per batch); VU1 transforms, projects and emits GIF packets (PATH1), double-buffered in VU memory.
Colour and Z are now 32-bit and Z24 (two 640×448 frames + Z = 3.4 MB of the 4 MB of GS VRAM). HUD text goes over
PATH3 after the VIF chain (libdraw adds the 2048 primitive offset itself; passing screen coordinates + 2048 put the
text off-screen).

PCSX2 (`docs/renders/slice-vu1.png`, indicative only, not the console):

| | EE path (3.2) | VU1 path |
|---|---|---|
| Triangles drawn | 58 052 | 62 206 (+ Mario box) |
| EE time to draw | 173 ms (later 75-80 ms) | **2.09 ms** (4.17 ms after ~40 s of the same still frame, see below) |
| Logic (game_update) | — | 0.14-0.21 ms |
| Worst frame seen | — | 2.34 ms, 4.91 ms in the slower stretch |
| Frame rate | ~6 FPS | 60 FPS |

The draw time doubled after ~40 s with nothing on screen changing (same Mario position, same triangle count) and
stayed there; the EE time includes the wait for the previous frame's VIF DMA, so it follows PCSX2's VU/GS pacing.
Real numbers need the console (task 5.2).

VUMD is 3.96 MB at 32 B per vertex, non-indexed (the indexed MODL chunk stays for collision-free uses). Not
compacted: the slice fits; a 16-bit position + 32-bit colour layout would halve it when a full scene needs it.

## Runtime memory row (task 5.1)

Console at boot (`mem_report`) and HUD every frame (`RUNTIME … KB`, live coins/purple/pieces/Kuribo):

| Type | Bytes | Wii (Gateway 2) |
|---|---|---|
| render (VIF/GIF packets 576 KB, VU batch headers, libdraw buffers) | 657 408 | render targets 0.32 MB + GX FIFO 0.5 MB (stationed) |
| objects (100 purple coins, 10 coins, 3 star pieces as 16-B pickups; 4 Kuribo × 48 B) | 1 984 | PurpleCoin 373.5 KB, StarPiece 380.9 KB, Kuribo 47.2 KB |
| player | 1 232 | 296.6 KB |
| collision, scene (KCL, models read in place from the blob) | 0 | in the scene heap as model instances |
| **Runtime total** | **660 624 (0.63 MB)** | **scene heap 3.24 MB** |

Per object: PurpleCoin 16 B vs 2 567 B, Kuribo 48 B vs 12 073 B, player 1.2 KB vs 297 KB, all far under the
one-sixth targets of 1.2. The slice does much less per object (see "Differences from SMG"), so these are floors.

## Frame time over a fixed path (task 5.2) — PCSX2, by the user's decision

`engine/build.sh bench` builds `slice-bench.elf`: the pad is replaced by a 30 s script (`pad.c`, `#ifdef BENCH`):
forward with a jump every 1.5 s for 10 s, a right-hand circle with a spin every 2 s for 10 s, forward-left for 10 s.
Mario crosses to the far side of the planet (1317, −425, 1574 → −510, 290, −1982), collects 4 purple coins and
stomps 2 Kuribo (`docs/renders/slice-bench.png`). Frames 61-1860, EE COUNT cycles:

| Part | Average | Worst |
|---|---|---|
| Logic (minus KCL) | 0.04 ms | 0.05 ms |
| Collision (KCL queries) | 0.08 ms | 0.22 ms |
| Draw (EE builds the VIF chain) | 2.21 ms | 2.37 ms |
| GS finish (VIF chain + HUD drained, `render_end` before vsync) | 2.77 ms | 3.01 ms |
| **Total busy** | **5.10 ms** | **5.51 ms** |

60 FPS throughout, up to 70 984 triangles in view. PCSX2 does not time the VU1 and GS like the console, so the
"GS finish" row is the least reliable one; the user chose the emulator over the SCPH-75001 for this gate.

## Gate (task 6.1)

Spec formula, OceanPhantomCave 1 (phase 0c):

- **Memory** = Wii scene heap 7.81 MB × (slice 0.630 MB / Wii slice 3.24 MB = 0.194) = **1.52 MB**, against a room of
  1.45 (asset margin at R = 0.99) + 2.3 (render targets) + 2.6 (XFB + GX FIFO) = **6.35 MB → fits**. Without the
  fixed render buffers the slice ratio is 0.001 and the extrapolation ~8 KB.
- **CPU** = slice worst frame × object ratio (Wii NameObjs 3 621 / 1 295 = 2.80): 5.51 ms × 2.80 = **15.4 ms**, under
  16.7 ms with 1.3 ms to spare. Scaling draw and GS time by objects is pessimistic (they follow triangles), but it is
  the spec's rule.

What the formula does not see (said plainly):
- It scales only the scene heap. The phase 0c trace found ~8 MB of *resident* runtime (stationed C++ objects 4.34 MB,
  other data ~3.6 MB: sound, camera, layouts, effects, save). The slice has none of those systems, so nothing measures
  their PS2 cost yet; that is the real open risk of a full engine.
- The slice is a subset: no Mario model/animation, no sound, particles, shadows, camera system, Bezier rails.
- Frame time is from PCSX2, not the console.

**Verdict: GO** for the full engine (memory 1.52 of 6.35 MB, CPU 15.4 of 16.7 ms extrapolated, PCSX2). The CPU margin
is thin: the full-engine plan has to keep the render path on VU1, budget the resident systems the slice lacks, and
re-measure on the console when one is at hand.
