# Where phase 2 stands (resume notes)

Last updated 2026-10-07. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

## Done

- Milestones 1-4 GO (resident budget, textures, Mario, cameras).
- Milestone 5, task 5.1 done: every galaxy/scenario converts into streaming packs (`tools/slice_all.py`).
  With every class that has a model converted and indexed geometry: **136 / 136 scenarios fit 9.01 MB** with
  streaming (worst OceanPhantomCave 1, 6.99 MB).
- Task 5.2 in progress. The engine plays any galaxy from its packs:
  - world space; gravities point, plane, plane-in-box / cylinder, cube, disk, segment;
  - 12 camera types;
  - zone streaming with prefetch and per-zone spheres;
  - texture cache in GS memory, frustum culling, indexed geometry;
  - launch stars, StarPieceGroup;
  - breakables: PunchBox, WoodBox, CrystalCage S/M;
  - CollapsePlane, SpaceMine, NeedlePlant;
  - Mario slopes as in Petari (confirmed by the user);
  - Mario's life: 3, one lost per hit, a coin gives one back, a miss (0 life or fell into space) puts him back at the start with full life (`LIFE` on the HUD).
- Every other placed class with a model is drawn and collided as a static instance.

## How to run

| What | Command (from Windows, `MSYS_NO_PATHCONV=1` in Git Bash) |
|---|---|
| Convert one scenario (embedded blob) | `python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin` |
| Convert everything (packs + table) | `python tools/slice_all.py data/fs/files out/packs 6` then `python tools/vram_need.py out/packs` |
| Build, embedded Gateway 2 | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench]` → `engine/slice[-bench].elf` |
| Build from packs | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh MODE out/slice/hd2.bin Galaxy/n` → `out/slice-packs[-MODE].elf` (run from `out/`: `host:packs/`) |
| Modes | `plain`, `bench`, `slope`, `zone` (launch star 0 at frame 120), `box`, `collapse`, `hazard` (EggStarGalaxy/1: the third NeedlePlant prick is a miss) |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| PCSX2 in the background | `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, `tools/snapbg.ps1 <png>`, log `Documents/PCSX2/logs/emulog.txt` |

Patch scripts go through files (Write), not bash heredocs, because of apostrophes. In Python-generated C, write `\n` as `chr(92) + "n"`.

## Next

1. Lumas / NPCs, coins popping out (health done: no LifeUp, no lives counter / game over yet).
2. More classes by placement count (`out/census.json`), e.g.:
   - GCaptureTarget, FlowerGroup, CutBushGroup;
   - ShootingStar, StarPieceSpot, BenefitItemOneUp;
   - EarthenPipe, SpinDriver, Kinopio;
   - QuestionCoin, StrongBlock, RailCoin, FlipPanel.
3. Transparency / blending, which would let `NO_DRAW` in `tools/slice.py` go.
4. Bezier rails (straight segments today), StarPieceFlow, CrystalCageL and power stars.
5. Task 5.3 gate: coverage table (plays / does not fit / missing classes), worst frame on the heaviest scenario,
   videos.
