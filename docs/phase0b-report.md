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
