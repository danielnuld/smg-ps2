# Phase 1 report (in progress)

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
