# Phase 0c report

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

## SPU2 budget (task 2.1)

| Item | Bytes | Source |
|---|---|---|
| SPU2 RAM | 2 097 152 | ps2tek:111 |
| System areas + MEMIN, halfwords 0x0000-0x27FF | −20 480 | ps2tek:3457-3470 (MEMIN, V); lower part E |
| Reverb, SPACE on one core (largest real reverb; echo/delay are 192 KB but are echoes) | −126 336 | libsd effect.c:27 |
| Music | 0 | ADMA: PCM straight into MEMIN, no sample RAM (ps2tek:3453) |
| **Sample budget for curated waves** | **1 950 336 (1.86 MB)** | |

Design choices behind it (D): reverb on one core only (the other core plays dry voices and ADMA); music through ADMA,
two stereo pairs over the two cores = the 4 stream channels SMG uses (`AudParams.cpp` `numStreamChannels = 4`). ADMA
runs at the SPU2 output rate, so `.ast` streams (32 kHz on the Wii, `AudParams.cpp` `streamSampleRate`) are resampled
offline; their buffers live in IOP or EE RAM (task 2.4), not in SPU2. With reverb HALL instead of SPACE the budget is
1.89 MB; with no reverb 1.98 MB.

## Which waves a scene can play (task 2.2) — `tools/audio.py` `waves()` / `referenced()`

Static chain from the game's tables: scene → loaded wave banks (`AudSceneMgr`) → instrument banks bound to them
(`bnk ` commands) → instruments and percussion → wave ids → archive and size (WSYS ctrl table).

Result: **every one of the 1 796 waves is referenced by some instrument or percussion region** (22.95 MB as SPU ADPCM,
all referenced; cross-check: 0 referenced ids missing from their bank). Bank 34 (player voices) has 320 waves in 3
archives sharing 160 ids. So the tables cut nothing: by them a scene can play every wave of its loaded banks, the
12.06–13.09 MB of 0b.

What the tables cannot say is which instruments are actually played. Two ways to learn it:

1. **Music vs effects split** (static): disassemble the BMS sequences (BGM in `AudioRes/Seqs/JaiSeq.arc`, effects in
   the BSC collection) following jumps, and collect the banks each one selects (`cmdBank` / `cmdBankPrg`). A bank used
   only by sequenced music leaves SPU2 if that music is pre-rendered to an ADMA stream. Bank 2 (4.65 MB, always
   loaded) is the one to check first.
2. **Observed use** (Dolphin): log the waves started per scene while playing. Exact, needs play time.

## Music vs effects (task 2.2b) — `tools/bms.py` + `audio.py` `sequences()` / `reached()`

`bms.analyse` follows each sequence's flow (jumps, calls, child tracks, both sides of conditionals) carrying the
current bank and program, and records the (bank, program) of every note. Programs resolve to waves through
`instruments()` (IBNK number = word 2). 105 BGM sequences, 3 245 SE sequences; 50 BGM and 63 SE paths ran off their
data (counted as `bad`, path dropped), 2 jump tables not followed.

| | Notes with a bank | Notes with no static bank | Waves reached | PS2 MB |
|---|---|---|---|---|
| BGM (sequenced music) | mostly IBNK 0, 1, 8, 9 → wave banks 0, 1, 10, 23 | 15 798 | 194 | 3.05 |
| SE (effects) | IBNK 64, 72, 74, 65, … (64 → wave bank 2 is the top user) | 19 583 | 1 349 | 18.35 |
| Music only (not reached by any SE) | | | 100 | **1.73** |

The music banks match the names (`B00mahit`/`B01mahit` = the composer's banks) and the BGM wave sets in
`AudSceneMgr` (`cBgmWaveSet` = 10, 23). Bank 2 (`B64kawa`, 4.65 MB, always loaded) is **effects**, not music. Some BGM
notes name banks 2-7 and 10-19, which have no IBNK; they resolve no wave.

Pre-rendering all sequenced music to ADMA streams and dropping the music-only waves:

| Max sample rate | Always loaded | Worst scene (KoopaBattleVs3 0) |
|---|---|---|
| original | 10.48 MB | 11.50 MB |
| 16 kHz | 7.75 | 8.74 |
| 11 025 Hz | 5.46 | 6.15 |
| 8 kHz | 4.00 | 4.50 |

Against the 1.86 MB SPU2 budget, **the effects alone are 2.4× over even at 8 kHz** (6× at the original rate). The
game's data cannot say which effects a scene really uses (19 583 SE notes take their bank from outside the sequence,
and actors pick sounds in code). Task 2.3 therefore needs either per-scene use observed in Dolphin, or a cut list
decided by hand.

### Correction to 2.2b (decoder bug)

`cmdWait` (0xF0) reads a MIDI varint inside its handler, and `cmdPrintf` (0xFD) a string; the first version of
`bms.py` skipped neither, so the byte after every wait was decoded as a note. With both fixed (selftest: "wait 1 is
not a note"), and notes resolved per key region instead of whole programs: 0 paths run off their data (was 113), and
only 23 SE sequences (17 remix notes, 6 HOME menu) leave their bank unset (was 2 967). Corrected split:

| | Waves | PS2 MB |
|---|---|---|
| BGM sequences reach | 168 | 2.69 |
| SE sequences reach | 1 336 | 18.14 |
| Music only | 134 | 2.09 |
| Played by no sequence (unused key regions) | 166 | 1.50 |

## Hand-cut effects set (task 2.3) — `python tools/sfxcut.py data/fs/files out/s099/scenario_sets.json`

Per scenario, the effects kept are (`tools/sfxcut.py`, all rules D):

1. **Mario everywhere**: PLAYER_VOICE, PLAYER_MOTION and REMIX_SEQ whole.
2. **Global code**: SE names written in `Game/{Player,Screen,System,Scene,Camera,Util,AudioLib,Demo,LiveActor,Effect,
   Gravity,NameObj,Speaker,RhythmLib}` (182 names). SYSTEM sounds count only through code, like the rest.
3. **Placed only**: SE names in the `.cpp` of the classes the scenario's placed objects create (NameObjFactory
   template argument), plus every class those files `new`, transitively; StageEffectDataTable entries for placed
   names; sounds named by the `.bas` animation tables in the archives the scenario loads.
4. **Dropped**: HOME_BUTTON_MENU (Wii only), DEMO (cutscene sounds ride in the cutscene's pre-rendered stream), music
   (pre-rendered to ADMA streams), and every sound none of the above names.
5. **Resampled**: player and system to ≤ 8 kHz, everything else to ≤ 6 kHz; a wave shared by several kept sounds
   keeps the highest cap. A note whose key is read from a register (random voice variant) keeps one variant.

Cut list: `docs/sfx-cut.csv`, one row per SE name: 1 695 kept where their class is placed, 267 kept everywhere
(whole category), 151 kept through global code, 1 083 dropped with no known user, 49 dropped by category.

How each rule moved the worst scenario (SPU ADPCM):

| Step | Worst scenario | MB | Over the 1.86 MB budget |
|---|---|---|---|
| All loaded banks (0b) | KoopaBattleVs3 0 | 13.09 | 136 / 136 |
| Placed/global/category filter, original rates | PhantomGalaxy 3 | 6.64 | 136 |
| + rates: player/system 11 kHz, rest 8 kHz | PhantomGalaxy 3 | 2.93 | 136 |
| + one variant per random voice + player 8 kHz | EggStar 3 | 2.30 | 63 |
| + rest at 6 kHz (**chosen**) | EggStar 3 | **1.91** | **1** |

EggStar 3 is 0.05 MB over with reverb SPACE; with reverb ROOM (19 KB instead of 123 KB, libsd effect.c:27) the
budget is 2.06 MB and all 136 fit. Median scenario 1.57 MB.

**Cost, said plainly:** 6 kHz keeps only content under 3 kHz: objects, enemies and ambience will sound muffled; Mario
and the UI at 8 kHz (under 4 kHz) lose their brightness. Random voice variants collapse to one. Risks of the static
filter: a sound played by a helper class reached other than by `new` (templates, function tables), or named by a
string built at run time, is dropped and would be silent; the 17 remix-note sounds take their instrument from the
current music, which is pre-rendered, so they need their own small sample set. Upgrade path for both: log the SE ids
played per scenario in Dolphin and add what is missing.

## EE-side audio (task 2.4)

Non-wave data, measured from the BAA (`SMR.szs` unpacked 596 320 B; block sizes from each WSYS/IBNK header, ranges
for the others) and the `AudioRes/Info`, `Seqs` archives (files unpacked):

| Data | KB | In PS2 RAM? (D) |
|---|---|---|
| WSYS wave tables (48) | 206.5 | yes (a compact SPU-address table would be ~28 KB: 1 796 waves × 16 B, E) |
| IBNK instrument banks (48) | 84.6 | yes |
| BSC effect sequences | 159.3 | yes (all kept; pruning the dropped effects would shrink it) |
| BST sound table | 37.8 | yes |
| BSTN sound names | 91.7 | no if the port turns sound names into ids at build time; counted here |
| JaiChord + JaiMe + JaiRemixSeq | 113.5 | yes |
| JaiSeq BGM sequences | 887.2 | **no**: music is pre-rendered to ADMA streams |
| **Total in EE RAM** | **693.4 (0.68 MB)** | 0.42 MB without BSTN and with compact wave tables |

**Stream buffers:** music plays through ADMA from IOP RAM (2 MB, ps2tek:84), not EE RAM (D). Bandwidth: 2 stereo
pairs of PCM16 at 48 kHz = 384 000 B/s from storage (E: USB-1.1 mass storage throughput on the SCPH-75001 not
measured). How much IOP RAM is free after the IOP modules is not measured (E).

**For the gate:** EE audio = **0.68 MB** (raw data size, E: JAudio's parsed objects and track pools live in the audio
heap; `heapwalk.py` reads its real use from the Dolphin dumps, task 3) instead of the 1.88 MB Wii heap bound used in
0b and in task 1.4.

Risk: some gameplay follows the music's beat (`AudBgmRhythmStrategy`, `RhythmLib`). With pre-rendered music the
port needs the beat timing as a small table per track instead of reading it from the sequencer.

## Gate (task 4.1)

Margin per scenario (spec: Phase 0c gate) = 31 − 6.71 (code) − resident − 0.68 (EE audio, task 2.4) −
min(scenario, streaming), with `--levers --scenario --vu-index=255 --simplify=R`. **The scene heap is not measured**
(tasks 3.1-3.2 skipped at the user's request), so every margin below is the room left for it. The spec does not allow
heap = 0, so this gate cannot say GO; it says how much heap each choice tolerates.

| Geometry | Resident | Worst scenario | Margin | Median | Scenarios that still fit with a heap of 1 / 2 / 3 / 4 MB |
|---|---|---|---|---|---|
| R = 0.99 (re-strip only, no visible change) | 13.95 | OceanPhantomCave 1 | **+1.45** | +5.65 | 136 / 131 / 128 / 118 |
| R = 0.75 (visible on curved models, task 1.5) | 13.45 | OceanPhantomCave 1 | +2.61 | +6.45 | 136 / 136 / 131 / 128 |
| R = 0.5 | 12.96 | OceanPhantomCave 1 | +3.70 | +7.20 | 136 / 136 / 136 / 131 |

The tightest scenarios are always OceanPhantomCave 1-4 and Phantom 1 (GhostShipCaveZone and TeresaHouseZone).

Audio (SPU2): the hand-cut effects set fits 135/136 scenarios with reverb SPACE and 136/136 with reverb ROOM
(task 2.3), at 8 kHz for Mario and the UI and 6 kHz for everything else; music and cutscene sound as streams.

**Verdict: not decided — conditional on the scene heap.**

- With no visible geometry change (R = 0.99): **GO-pending-CPU if the heap of the heaviest scenes is ≤ 1.45 MB**.
- Accepting the R = 0.75 artefacts: GO-pending-CPU if the heap is ≤ 2.61 MB.
- If the heap is larger than 2.6 MB in OceanPhantomCave/Phantom: NO-GO for those scenarios with these rules.

## Task 4.2

Neither GO nor NO-GO, so `phase-1-host-port` is not opened. One measurement decides it: MEM1/MEM2 dumps of
OceanPhantomCave 1, 2, 4 and CosmosGarden 2 in Dolphin, read with `python tools/heapwalk.py mem1.raw mem2.raw`
(scene exp heap + audio solid heap). Costs already accepted to get here, for the record: audio at 6-8 kHz with one
voice variant, music and cutscene sound pre-rendered, a VU1 microcode for indexed geometry (N = 255, cycle cost not
measured), the beat-timing table for rhythm gameplay, and the remix-note samples.

## Heap measured (after the gate, 2026-10-06)

How (`tools/dolphin/README.md`): a 121-star save (PAL `data.bin` from savegameworld.com, "Super Mario Galaxy by
ILoveVideoGame", decrypted and placed under the RMGE title; it loads as "Complete! ★121"); Dolphin 2609 driven by
key/mouse input; the galaxy chosen by rewriting the next `SceneControlInfo` when "Play This File" requests the
observatory; MEM1/MEM2 dumped 30 s after the stage loads; heaps named through the game's own `HeapMemoryWatcher`
table (`tools/heapreport.py`). Each stage was checked on screen.

| Scene | File cache (archives) | Scene heap MEM1 | Scene heap MEM2 | **Scene heap total** |
|---|---|---|---|---|
| AstroGalaxy (observatory) | 4.13 | 7.15 | 0.96 | 8.11 |
| OceanPhantomCave 1 | 11.76 | 5.52 | 2.29 | **7.81** |
| OceanPhantomCave 2 | 12.62 | 5.63 | 2.29 | **7.92** |
| OceanPhantomCave 4 | 11.37 | 5.49 | 2.29 | **7.78** |
| CosmosGarden 2 | 15.17 | 5.79 | 2.69 | **8.48** |
| Phantom 1 | 12.06 | 4.76 | 1.91 | **6.67** |

Every scene also keeps the stationed heaps full (6.69 MB MEM1 + 19.30 MB MEM2) and the audio heap (1.87 MB).

The file cache holds the scene's archives (what `budget.py` sizes); archives overflow into the scene heap only below
3 % free (`MemoryUtil.cpp:51-60`) and the fullest cache here is 93 %. So the scene heap is runtime memory: actors,
model instances and the J3D structures built from the archives, collision, effects, layouts.

Margin minus the measured scene heap:

| Scenario | R = 0.99 | R = 0.75 | R = 0.5 |
|---|---|---|---|
| OceanPhantomCave 1 | +1.45 − 7.81 = **−6.36** | −5.20 | −4.11 |
| OceanPhantomCave 2 | −6.46 | −5.29 | −4.19 |
| OceanPhantomCave 4 | −6.12 | −5.00 | −3.94 |
| CosmosGarden 2 | −3.88 | −3.02 | −2.23 |
| Phantom 1 | −5.10 | −3.96 | −2.89 |

**Verdict: NO-GO.** With the Wii's runtime memory per scene, no scenario measured fits, at any R. And this is the
optimistic side: the stationed heaps hold 26 MB on the Wii, while the budget's resident row counts only their
archives (17.27 MB Wii); the other ~8.7 MB of resident runtime objects (E: if any archive stays compressed in RAM
the share is larger) is not in the PS2 budget at all.

What would have to be true to retry: the PS2 port's runtime structures per scene at **≤ 19 %** of the Wii's scene
heap (R = 0.99) or ≤ 47 % (R = 0.5), *and* the resident runtime objects shrunk the same way. The scene heap mixes
derived model data (which a PS2 asset format would replace) with plain actor objects (32-bit pointers on both CPUs,
so similar size); splitting it needs an allocation trace by type (Dolphin breakpoints on JKRHeap::alloc with the
caller's return address), which is the next measurement if this is ever reopened.
