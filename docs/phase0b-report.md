# Phase 0b report (in progress)

## Layers, checked by hand (task 1.2) — HeavensDoorGalaxy (Gateway)

`ScenarioData.bcsv`: every zone column is 1 in scenario 1 and 2 in scenario 2 → masks Common+LayerA and Common+LayerB.

| Scenario | Zones placed (main stage `StageObjInfo`) | Objects in the active zone layers |
|---|---|---|
| 1 | MysteriousZone (common), BlackHole, Inside, Middle, Small (layera) | Inside layera: GrandStar; Mysterious layera: Rosetta, DemoRabbit… |
| 2 | MysteriousZone only | Mysterious layerb: CirclePurpleCoinGroup, PowerStar, Kuribo… (68 rows) |

`HeavensDoorLargeZone` is in `ZoneList.bcsv` but no stage places it, so no scenario loads its objects; phase 0 counted it.
Scenario 2 is the purple-coin mission (the coins sit in layerb), which matches the mask reading.

## Per-scenario budget (tasks 1.3–1.5) — `python tools/budget.py data/fs/files out/levers --levers --scenario`

Without `--scenario` the run rewrites `budget.csv` and `zones.csv` byte-identical to phase 0 (`cmp`, task 1.4).
Room = 31 − 6.71 − 15.28 (resident, levers) = **9.01 MB**. Per (galaxy, scenario): stage archives of the placed zones +
ObjectData archives named in their active `placement`/`mapparts` layers + `childobj` of every layer (game loads all).

| | Whole galaxy (phase 0) | Per scenario |
|---|---|---|
| Rows | 48 galaxies | 136 scenarios in 48 galaxies |
| Fit in 9.01 MB | 34 / 48 | **100 / 136** |
| Worst | EggStarGalaxy 16.95 | CosmosGardenGalaxy sc. 2 14.00 |
| Median | 4.35 per galaxy | 4.34 worst scenario per galaxy (6.66 over all 136 rows: big galaxies have 6 scenarios each) |

Gateway check: scenario 1 = 6 stages, 9.30 MB; scenario 2 = main + MysteriousZone, 3.21 MB (matches the hand check above). No scenario exceeds its whole-galaxy figure.

Still over the room (36 scenarios in 10 galaxies) → task 2:

| Galaxy | Scenarios over | Worst MB | Scenario numbers |
|---|---|---|---|
| CosmosGardenGalaxy | 6 | 14.00 | 1 2 3 4 5 6 |
| FactoryGalaxy | 6 | 13.51 | 1 2 3 4 5 6 |
| EggStarGalaxy | 6 | 12.92 | 1 2 3 4 5 6 |
| OceanPhantomCaveGalaxy | 5 | 11.10 | 1 2 3 4 5 |
| HellProminenceGalaxy | 6 | 11.04 | 1 2 3 4 5 6 |
| PhantomGalaxy | 3 | 10.62 | 1 2 3 |
| StarDustGalaxy | 1 | 9.58 | 2 |
| ReverseKingdomGalaxy | 1 | 9.56 | 3 |
| HoneyBeeKingdomGalaxy | 1 | 9.54 | 3 |
| HeavensDoorGalaxy | 1 | 9.30 | 1 |

Unresolved placement names now 0–2 per scenario (was ~50 per galaxy: camera/rail names no longer read). What is left are
real objects the factory tables do not map: StrayTico (25 scenarios), ChildKameck, TamakoroTutorial, BossKameckMoveRail,
ChildKuribo, NormalCommonMapParts, StoryDemoSimpleBloomArea, ChildIceMeramera, … → still an **undercount**, smaller.

## Does the game ever split zones? (task 2.1)

No. Read in Petari:

- `StageFileLoader` mounts every `ZoneList` archive at scene start and never unmounts one (`StageFileLoader.cpp:14-40`).
- `ViewGroupCtrl` is the only "what is near the player" mechanism: designer-placed `ViewGroupCtrlCube` volumes switch
  actors with a `ViewGroupId` to the full far-clip distance while the player is inside. It changes drawing, not memory.
- `ZoneSwitch` is per-zone switch flags, `addToZone` partitions collision. Neither loads anything.

So on the Wii every active zone of a scenario is resident at once, and splitting zones on PS2 is **new engine work**
(streaming), not a switch the game already has. Per design decision 4, a split needs evidence that one zone set is
never on screen while the player is in another. Candidate evidence for task 2.2, all checkable:

1. `ViewGroupCtrlCube` volumes in the overflowing scenarios: designer data saying which groups matter from where.
2. Travel between zones by launch star (`SuperSpinDriver`) or pipe: the camera cuts, which a load can hide behind,
   but only if the source zone is not visible from the target. Needs a Dolphin look per pair.
3. Dolphin free camera at the zone boundaries of the 6 large galaxies.

## Zone sets for the 36 scenarios over the room (task 2.2)

Two numbers per scenario, both in `scenarios.csv`:

- **pair** (`worst_zone_ps2`): the floor of any split = main stage + the heaviest zone (+ the stages that place it),
  shared archives once. Over all 36 the worst pair is 8.05 MB (OceanPhantomCave 1, GhostShipCaveZone), so **no scenario
  is beyond saving by a split**.
- **stream** (`stream_ps2`): a split that needs **no visibility evidence**: the current zone chain in full, every other
  active zone kept on screen with only its planets at their lowest LOD archive (`Low`, else `Middle`, else the model),
  and no actors there (actors beyond their far clip are not drawn, `ClippingJudge.cpp:39-41`). All stage archives stay.
  This is a design choice (D); bosses or other actors seen from afar would need their own entry.

Why not split on visibility: the disc data cannot show it. The main stage of each big galaxy holds most launch stars
(8-12 `SuperSpinDriver`) and `ViewGroupCtrlCube` volumes (23-55), so zones hang off a hub, but a launch-star flight
shows the destination approaching: it is not a camera cut. Visibility claims would need Dolphin, and the stream
figure makes them unnecessary for 31 of 36.

| Scenario | Whole | Pair (worst zone) | Stream | vs 9.01 |
|---|---|---|---|---|
| CosmosGardenGalaxy 2 | 14.00 | 2.96 (DandelionHillZone) | 5.84 | fits |
| CosmosGardenGalaxy 3 | 13.69 | 3.21 (QuestionBoxZone) | 5.07 | fits |
| FactoryGalaxy 3 | 13.51 | 3.71 (ChoConveyorZone) | 5.39 | fits |
| CosmosGardenGalaxy 6 | 13.14 | 2.66 (QuestionBoxZone) | 4.52 | fits |
| FactoryGalaxy 1 | 13.11 | 4.53 (MechanicKoopaZone) | 6.03 | fits |
| CosmosGardenGalaxy 1 | 13.03 | 2.20 (QuestionBoxZone) | 5.11 | fits |
| EggStarGalaxy 3 | 12.92 | 2.53 (OtaKingStageZone) | 4.88 | fits |
| CosmosGardenGalaxy 4 | 12.38 | 2.67 (DandelionHillZone) | 5.45 | fits |
| EggStarGalaxy 1 | 12.09 | 2.41 (SoramameZone) | 4.50 | fits |
| EggStarGalaxy 4 | 12.08 | 2.40 (SoramameZone) | 4.49 | fits |
| CosmosGardenGalaxy 5 | 11.69 | 1.91 (QuestionBoxZone) | 4.52 | fits |
| EggStarGalaxy 2 | 11.53 | 2.56 (StarEggRoadZone) | 4.64 | fits |
| FactoryGalaxy 2 | 11.33 | 3.13 (MiniMechanicKoopaZone) | 4.54 | fits |
| EggStarGalaxy 5 | 11.27 | 2.37 (StarEggRoadZone) | 4.46 | fits |
| FactoryGalaxy 4 | 11.10 | 3.13 (MiniMechanicKoopaZone) | 4.54 | fits |
| OceanPhantomCaveGalaxy 2 | 11.10 | 7.76 (GhostShipCaveZone) | 9.86 | **over** |
| HellProminenceGalaxy 3 | 11.04 | 3.20 (HellShellZone) | 5.61 | fits |
| FactoryGalaxy 5 | 10.86 | 2.87 (MiniMechanicKoopaZone) | 4.27 | fits |
| FactoryGalaxy 6 | 10.77 | 2.86 (MiniMechanicKoopaZone) | 4.27 | fits |
| HellProminenceGalaxy 2 | 10.75 | 3.21 (HellShellZone) | 5.74 | fits |
| PhantomGalaxy 3 | 10.62 | 6.28 (TeresaHouseZone) | 8.25 | fits |
| HellProminenceGalaxy 1 | 10.57 | 4.13 (HellShellZone) | 6.54 | fits |
| EggStarGalaxy 6 | 10.43 | 1.79 (DiskGardenZone) | 3.89 | fits |
| OceanPhantomCaveGalaxy 1 | 10.27 | 8.05 (GhostShipCaveZone) | 9.83 | **over** |
| PhantomGalaxy 1 | 10.25 | 7.51 (TeresaHouseZone) | 9.47 | **over** |
| HellProminenceGalaxy 4 | 10.09 | 3.92 (HellShellZone) | 6.33 | fits |
| OceanPhantomCaveGalaxy 4 | 9.94 | 7.72 (GhostShipCaveZone) | 9.50 | **over** |
| OceanPhantomCaveGalaxy 3 | 9.70 | 7.53 (GhostShipCaveZone) | 9.31 | **over** |
| StarDustGalaxy 2 | 9.58 | 5.03 (ThrowTutorialZone) | 6.46 | fits |
| HellProminenceGalaxy 6 | 9.57 | 3.35 (HellShellZone) | 5.76 | fits |
| ReverseKingdomGalaxy 3 | 9.56 | 5.56 (ReverseForestHomeZone) | 7.80 | fits |
| HellProminenceGalaxy 5 | 9.56 | 3.13 (HellShellZone) | 5.53 | fits |
| HoneyBeeKingdomGalaxy 3 | 9.54 | 4.80 (ForestHomeZone) | 6.90 | fits |
| HeavensDoorGalaxy 1 | 9.30 | 3.72 (HeavensDoorMysteriousZone) | 6.37 | fits |
| PhantomGalaxy 2 | 9.23 | 5.90 (TeresaHouseZone) | 7.86 | fits |
| OceanPhantomCaveGalaxy 5 | 9.22 | 6.95 (GhostShipCaveZone) | 8.73 | fits |

**Stream fits 31 / 36**, so with per-scenario loading **131 / 136** scenarios fit. Over: OceanPhantomCave 1-4
(GhostShipCaveZone, up to 9.86) and Phantom 1 (TeresaHouseZone, 9.47) → task 2.3. These margins are before heap and
audio; the gate subtracts those.

## One more lever: indexed geometry expanded on VU1 (task 2.3)

The streaming sets that overflowed are geometry-bound (OceanPhantomCave 2 / GhostShipCaveZone: geo 5.64 of 9.86 MB;
Phantom 1 / TeresaHouseZone: 5.47 of 9.47), because phase 0 de-indexes every emitted vertex. Lever `--vu-index=N`
keeps geometry indexed in RAM; VU1 (16 KB data memory, ps2tek:82) expands batches of at most N unique vertices
(sources: `--vu-index` row, D). N is a knob; the VU1 cycle cost is not measured.

Per scenario the better of whole (no streaming) and streaming counts. `python tools/budget.py data/fs/files out/vuN --levers --scenario --vu-index=N`:

| Geometry | Resident (geo) | Room | Scenarios that fit | Worst | Margin before heap/audio |
|---|---|---|---|---|---|
| de-indexed (`--levers`) | 15.28 (4.15) | 9.01 | 131 / 136 | OceanPhantomCave 2, 9.86 | −0.84 |
| `--vu-index=64` | 15.00 (3.87) | 9.29 | 133 / 136 | OceanPhantomCave 2, 9.68 | −0.39 |
| `--vu-index=128` | 14.73 (3.60) | 9.56 | **136 / 136** | OceanPhantomCave 1, 9.41 | **+0.16** |
| `--vu-index=255` | 14.44 (3.31) | 9.85 | **136 / 136** | OceanPhantomCave 1, 9.09 | **+0.76** |

Every scenario fits from N = 128 on, but the margin is what heap and audio must live in: 0.16-0.76 MB, against a Wii
audio heap of 1.88 MB and a scene heap not yet measured. N = 255 means a pool of ~7 KB at a ~28-byte stride: double
buffered it takes ~14 of VU1's 16 KB (estimate, E). The memory question now sits on task 3 (heap) and task 4 (audio).

## Runtime heap tool (task 3.1)

`tools/heapwalk.py mem1.raw mem2.raw` (Dolphin memory view -> Dump MEM1 / Dump MEM2). It lists every JKRExpHeap
used-block chain (one per heap; blocks whose links disagree with their neighbours are dropped) and every JKRHeap
object found at the start of a used block, with **used** bytes: `mSize − mFreeSize` for solid heaps, the largest chain
inside the heap for exp heaps. No symbols needed, so RMGE01 works. Selftest: synthetic chains, a nested heap, a stale
`'HM'`, a solid heap. The scene heap and the 1.88 MB audio solid heap both show up there (task 3.3).

## Audio (tasks 4.1–4.2) — `python tools/audio.py data/fs/files`

4.1: the bound decides. Every margin left after task 2.3 (0.16–0.76 MB) is below the 1.88 MB audio heap, so the split
was measured (4.2). What the split showed is bigger than the EE question:

**Wave data.** On the Wii waves do not live in the 1.88 MB heap but in a 14 MB "ARAM" heap carved out of MEM2
(`AudParams.cpp:21`). Banks from the BAA `ws  ` commands, archives from each WSYS, which banks from `AudSceneMgr`:

| | Wii (.aw) | PS2 SPU ADPCM |
|---|---|---|
| Always loaded (banks 0-8, 11, Mario voices) | 12.24 MB | 12.06 MB |
| Worst scene (LibraryRoom / AstroDome) | 13.25 MB | 13.09 MB |
| Worst scene, resampled to ≤ 16 kHz | | 9.72 MB |
| ≤ 11 025 Hz | | 6.83 MB |
| ≤ 8 kHz | | 4.99 MB |

SPU2 RAM is 2 MB in total (ps2tek:111), before reverb work area and stream buffers. Waves are already 16-22 kHz on
the Wii (97 % ADPCM); even at 8 kHz the worst scene needs 2.5× the SPU2. The biggest bank (2, `B64kawa_0.aw`, 4.55 MB)
is always loaded. **The waves cannot be resident in SPU2**, and the EE has no room for the overflow (margin
0.16–0.76 MB). Sound would need its own design: SPU2 as a cache over samples streamed from storage, or cutting/merging
banks. Neither is measured here.

**EE side (non-wave).** Bound 1.88 MB (the Wii solid heap). Its real use is read with `heapwalk.py` in task 3.3.
