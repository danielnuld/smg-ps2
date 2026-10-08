# Where phase 2 stands (resume notes)

Last updated 2026-10-07. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

## Done

- Milestones 1-4 GO (resident budget, textures, Mario, cameras).
- Milestone 5, task 5.1 done: every galaxy/scenario converts into streaming packs (`tools/slice_all.py`).
  With every class that has a model converted and indexed geometry: **136 / 136 scenarios fit 9.01 MB** with
  streaming (worst OceanPhantomCave 1, 6.98 MB).
- Task 5.2 in progress. The engine plays any galaxy from its packs:
  - world space; gravities point, plane, plane-in-box / cylinder, cube, disk, segment;
  - 12 camera types;
  - zone streaming with prefetch and per-zone spheres (a pipe's exit zone is wanted at once);
  - texture cache in GS memory, frustum culling, indexed geometry;
  - launch stars: SuperSpinDriver (+ Pink / Green), SpinDriver (rail flight or a throw along its Y axis);
  - coins: Coin, RailCoin, CircleCoinGroup and the purple ones, StarPiece, StarPieceGroup;
  - breakables: PunchBox, WoodBox, CrystalCage S/M (spin or hip drop), HipDropRock (hip drop only);
  - CollapsePlane, SpaceMine, NeedlePlant, BlackHole / BlackHoleCube (miss), ElectricRail (shock unless squatting);
  - items taken on touch: PowerStar (counted, spins), 1-up / life-up mushrooms, QuestionCoin;
  - EarthenPipe pairs (arg3), instant warp and the partner's exit jump;
  - stage switches: per-zone (< 1000) and global ids, appear switches hide / show rows (instances, coins, star pieces,
    launch stars), SW_DEAD on kills, Coin SW_B kills; logic rows TimerSwitch, SwitchSynchronizerReverse,
    GroupSwitchWatcher (`out/switch_census.json`: which classes use which switch);
  - Mario: slopes as in Petari (confirmed by the user), hip drop, life 3 (6 after a life-up), a coin gives one back,
    lives 4, a miss (0 life, fell into space, black hole) costs one and restarts at the start; HUD LIFE / STARS / LIVES.
- Every other placed class with a model is drawn and collided as a static instance.

## How to run

| What | Command (from Windows, `MSYS_NO_PATHCONV=1` in Git Bash) |
|---|---|
| Convert one scenario (embedded blob) | `python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin` |
| Convert everything (packs + table, ~5 min) | `python tools/slice_all.py data/fs/files out/packs 6` then `python tools/vram_need.py out/packs` |
| Build, embedded Gateway 2 | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench]` → `engine/slice[-bench].elf` |
| Build from packs | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh MODE out/slice/hd2.bin Galaxy/n` → `out/slice-packs[-MODE].elf` (run from `out/`: `host:packs/`) |
| Modes | `plain`, `bench`, `slope`, `zone` (first SuperSpinDriver at frame 120), `box`, `collapse`, `hazard` (EggStar 1: third NeedlePlant prick is a miss; BattleShip 2: mine, electric rail, black hole), `item` (BattleShip 2; CosmosGarden 1: a QuestionCoin's dead switch brings star pieces), `spin` (BattleShip 2, CannonFleet 1), `pipe` (BattleShip 2), `hip` (Factory 1) |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| PCSX2 in the background | `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, `tools/snapbg.ps1 <png>`, log `Documents/PCSX2/logs/emulog.txt` |

Patch scripts go through files (Write), not bash heredocs, because of apostrophes. In Python-generated C, write `\n` as `chr(92) + "n"`.
The OBJS row format (`ROW` in `tools/slice.py`, `Row` in `engine/src/blob.h`) changed to 92 B (switches): any change
there needs the embedded blob and every pack rebuilt.

## Next

1. More switch sources and users: PunchBox SW_A, IronCannonLauncherPoint, StarPieceSpot, Kinopio / Tico, HipDropSwitch,
   PicketSwitch, FlipPanel / FlipPanelObserver, hidden EarthenPipes (mode 1 / 2).
2. Lumas / NPCs, coins popping out of boxes instead of appearing in place.
3. More classes by placement count (`out/census.json`): GCaptureTarget, FlowerGroup, CutBushGroup, ShootingStar,
   Kinopio, FlipPanel, LavaSparks / LavaSteam / FireBar hazards.
4. Transparency / blending, which would let `NO_DRAW` in `tools/slice.py` go (electric rails and black holes need it
   to look right).
5. Bezier rails (straight segments today), StarPieceFlow, CrystalCageL.
6. Task 5.3 gate: coverage table (plays / does not fit / missing classes), worst frame on the heaviest scenario,
   videos.
