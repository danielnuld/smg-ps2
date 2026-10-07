# Phase 0b report (in progress)

## Layers, checked by hand (task 1.2) — HeavensDoorGalaxy (Gateway)

`ScenarioData.bcsv`: every zone column is 1 in scenario 1 and 2 in scenario 2 → masks Common+LayerA and Common+LayerB.

| Scenario | Zones placed (main stage `StageObjInfo`) | Objects in the active zone layers |
|---|---|---|
| 1 | MysteriousZone (common), BlackHole, Inside, Middle, Small (layera) | Inside layera: GrandStar; Mysterious layera: Rosetta, DemoRabbit… |
| 2 | MysteriousZone only | Mysterious layerb: CirclePurpleCoinGroup, PowerStar, Kuribo… (68 rows) |

`HeavensDoorLargeZone` is in `ZoneList.bcsv` but no stage places it, so no scenario loads its objects; phase 0 counted it.
Scenario 2 is the purple-coin mission (the coins sit in layerb), which matches the mask reading.
