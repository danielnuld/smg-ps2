# Phase 0 report

## Disc (tasks 2.1–2.3)

- Image: `data/smg.rvz`, RVZ lossless. `nodtool info`: Game ID **RMGE01** (USA), disc 1, rev 0. Not the RMGK01 Petari targets; fine for sizing.
- `nodtool extract -p data`: 2382 files, 3 391 778 585 B. Biggest folders: MovieData 2.3 G, AudioRes 747 M, ObjectData 354 M, StageData 35 M (`du -sh`, exFAT blocks).

## Memory (task 3.5) — `python tools/budget.py data/fs/files out`

All MB = decompressed bytes / 2^20, per the estimate rules in `sources.md` (D/E rows).

| Item | Wii | PS2 est. | Notes |
|---|---|---|---|
| main.dol text+data+bss | 6.71 | ? | PPC sizes from DOL header (E); MIPS size unknown |
| Resident set (184 archives, UsEnglish) | 17.3 | 17.0 | other 10.95 (anims/effects/fonts), tex 3.10, geo 2.90. Top: MarioAnime 2.67, Effect 2.35, Font 2.08 |
| Worst galaxy: EggStarGalaxy (18 zones) | 17.0 | 16.3 | 137 unresolved placement names → **undercount** |
| HeavensDoorGalaxy (7 zones) | 9.8 | 9.5 | sanity sample; 61 unresolved |
| Median galaxy (48) | | 4.37 | |

Margin with today's rules: 31 − 6.71 − 17.0 = **7.29 MB** for a galaxy → 33 of 48 galaxies fit, but every galaxy figure is a lower bound (unresolved names) and audio is not counted.

## Resolution (2026-10-03)

Placement name -> archives now follows the game's own preload, `NameObjFactory::getMountObjectArchiveList`
(cCreateTable + cName2ArchiveNamesTable + makeArchiveList bodies + PlanetMapDataTable flags). EggStar unresolved 137 -> 56;
what is left is `未入力N` placeholders, rail/group names and test parts (the `name` column also exists in camera/rail BCSVs),
plus a few real gaps: `Child*` spawned by parents, `IceWall`, `KinokoSuper`.
Geometry PS2 size is now de-indexed per emitted vertex (`tools/geom.py`), which makes PS2 **bigger** than Wii.

## What eats memory (Wii, decompressed)

| | Resident 17.27 MB | EggStar 20.05 MB |
|---|---|---|
| Model geometry VTX1+SHP1 | 2.91 | 6.62 |
| Textures (TEX1 + bti) | 3.40 | 5.60 |
| Collision kcl (normals / prisms / octree / pos) | 0.71 | 4.29 (2.38 / 0.86 / 0.88 / 0.17) |
| Animations bck (rot s16 / table / trans f32 / scale f32) | 3.01 | 1.72 |
| Fonts brfnt | 2.08 | — |
| Particles jpc | 1.98 | — |

## Levers, measured on the data

| Lever | Resident | EggStar | St |
|---|---|---|---|
| De-index for PS2 (V3-16 pos, V3-16 nrm, V4-8 clr, 16-bit UV) | 2.91 -> 4.15 | 6.62 -> 8.92 | cost, D |
| Normals 8-bit (V3-8 not yet sourced) | -> 3.51 | -> 7.58 | E |
| Drop high LOD when Middle/Low exists (Middle becomes top) | 0.05 of geo | 4.52 of de-indexed geo | D |
| Textures max 128 + all PSMT4 (vs max 256 / bpp rule: 3.10, 4.80) | 3.40 -> 1.52 | 5.60 -> 2.15 | D, quality cost |
| kcl normals f32 -> s16 | -0.17 | -1.19 | D, precision not evaluated |
| bck kept zlib-9 in RAM, unpacked on play (MarioAnime 2.48 -> 1.50) | -0.98 | ~-0.67 (same ratio, E) | D |
| **Zone streaming** (galaxy loads all zones today, StageFileLoader.cpp:14-40) | — | 19.2 -> biggest zone 2.66 | D, engine change |

## All galaxies with `--levers` (`python tools/budget.py data/fs/files out/levers --levers`)

High LOD is dropped only when a **Middle** archive exists (Low-only would make Low the top model).
Room = 31 − 6.71 (main.dol, PPC) − resident.

| Mode | Resident | Room | Galaxies that fit whole | Median / max galaxy | Zones that fit | Worst zone |
|---|---|---|---|---|---|---|
| base rules | 18.20 | 6.09 | 24 / 48 | 6.36 / 21.56 | 225 / 234 | GhostShipCaveZone 11.37 |
| `--levers` | 15.28 | 9.01 | 34 / 48 | 4.35 / 16.95 | 233 / 234 | GhostShipCaveZone 9.02 |

Not counted: runtime heap (below), audio banks, MIPS code size (task 4.2), fonts/jpc levers.

## Code size on the EE (task 4.2) — `tools/ee_codesize.sh` + `tools/codesize.py`

galaxy-macos sources (host C++ port of Petari) built per TU with `mips64r5900el-ps2-elf-g++ 15.2 -Os -G0`
(`tools/ee_cc.sh`, shims in `tools/ee_include/`), then `ld -r` so inline/template COMDATs count once
(6743 inline sections, 0 duplicates). Compared against the same TUs' ranges in Petari `config/RMGK01/splits.txt`.

| | PPC (Wii) | MIPS (EE) |
|---|---|---|
| 1663 / 1802 TUs that compiled (84 % of ported PPC bytes) | 4.40 MB | 4.43 MB (×1.007) |
| Game + JSystem + nw4r + RVLFaceLib, extrapolated | 5.24 MB | ~5.28 MB (E) |
| SDK / MSL / MetroTRK / Runtime (replaced on PS2) | 1.37 MB | ps2sdk + newlib + GS renderer + audio: not measured |

Section split differs (MWCC puts strings/vtables in .data, GCC in .rodata), so only totals are compared.
`NO_INLINE` is dropped for GCC (attribute after a definition), which may inline a little more. 139 TUs still fail
(MSL `binder2nd`/`mem_func` vs libstdc++ among them). So keeping 6.71 MB for code in the room math is about right,
as long as the PS2 runtime libraries stay under the 1.37 MB the Wii SDK used (E).

## Wii runtime memory (what the archives do not show)

`HeapMemoryWatcher.cpp:139-152`: system 0x40000, audio 0x1E0000 (1.88 MB), stationed 0x900000 MEM1 + 0x1400000 MEM2
(then shrunk to use, `GameSystemStationedArchiveLoader.cpp:221`), game heaps = the rest. Per scene a file-cache heap of
0x1040400 (16.25 MB, `GameSystemSceneController.cpp:113`); archives overflow into the scene heap below 3 % free
(`MemoryUtil.cpp:51-60`), so 16.25 MB is not a cap. Actor instances live in the scene heap; their size can only be
measured with the game running (e.g. Dolphin + heap free sizes per galaxy).

## Open before the gate

2. Fonts (menufont64 853 KB, messagefont26 664 KB) and jpc textures: measure SD-size font sheets and PS2 texture rules on jpc.
4. Zone streaming: which zones are visible from which (low models of distant zones still need to be resident).
5. Runtime heap: measure scene-heap use per galaxy with the game running.

## Gate (task 5.1)

Margin = 31 − 6.71 (main.dol) − resident − galaxy (spec: Go/No-Go gate). Heaviest galaxy, EggStarGalaxy:

| Rules | Margin | Galaxies with negative margin |
|---|---|---|
| base | 31 − 6.71 − 18.20 − 21.56 = **−15.46** | 24 / 48 |
| `--levers` | 31 − 6.71 − 15.28 − 16.95 = **−7.93** | 14 / 48 |

Negative with `--levers`: EggStar −7.93, Factory −7.12, CosmosGarden −6.63, Phantom −6.25, HellProminence −5.00,
OceanPhantomCave −4.18, StarDust −3.16, CannonFleet −2.46, SandClock −2.13, HoneyBeeKingdom −1.99, BattleShip −1.98,
ReverseKingdom −1.48, IceVolcano −1.11, HeavensDoor −0.76.

**Verdict: NO-GO** for the game as it loads today (whole galaxy at once), even with every lever applied.

## What would have to be true to retry (task 5.2)

1. **Zone streaming.** With `--levers`, 233 / 234 zones fit in the 9.01 MB room, so the whole verdict hangs on loading
   a galaxy one zone at a time (an engine change to `StageFileLoader.cpp:14-40`). Needs open item 4: which zones must be
   resident together (visible neighbours, low models of distant zones). Then re-run the gate on "zone + its visible set".
2. **GhostShipCaveZone** (9.02 MB vs 9.01 room) needs one more lever: kcl s16 normals or bck-in-zlib on that zone alone.
3. **Runtime heap fits what is left** (open item 5): actor instances per scene, measured in Dolphin. The gate above has
   no room for it; zone streaming is what frees it.
4. **Audio is extra**: the Wii reserves 1.88 MB for it; on PS2 banks go to the 2 MB SPU2 RAM, so the EE-side cost must be
   measured, not assumed.
5. **PS2 runtime libraries stay ≤ 1.37 MB** (what the Wii SDK used), or the 6.71 MB code figure no longer holds.

Next step if retried: a change `phase-0b-zone-streaming` that measures items 1–3, not `phase-1-host-port`.
