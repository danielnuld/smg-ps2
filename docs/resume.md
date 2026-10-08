# Where phase 2 stands (resume notes)

Last updated 2026-10-08. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

## Done

- Milestones 1-4 GO (resident budget, textures, Mario, cameras).
- Milestone 5, task 5.1 done: every galaxy/scenario converts into streaming packs (`tools/slice_all.py`).
  With every class that has a model converted and indexed geometry: **136 / 136 scenarios fit 9.01 MB** with
  streaming (worst OceanPhantomCave 1, 6.98 MB).
- Task 5.2 in progress. The engine plays any galaxy from its packs:
  - world space; gravities point, plane, plane-in-box / cylinder, cube, disk, segment;
  - 12 camera types;
  - zone streaming with prefetch and per-zone spheres (a pipe's exit zone is wanted at once), from `host:` or from
    the **disc image** (`tools/disc.py`: one sector-aligned PACKS.BIN + PACKS.TOC, read with libcdvd, prefetch
    asynchronous: no slow frame while streaming; ~1.5-2 MB/s in PCSX2, a zone wanted at once blocks ~1 s);
  - texture cache in GS memory, frustum culling, indexed geometry;
  - launch stars: SuperSpinDriver (+ Pink / Green), SpinDriver (rail flight or a throw along its Y axis);
  - coins: Coin, RailCoin, CircleCoinGroup and the purple ones, StarPiece, StarPieceGroup;
  - breakables: PunchBox, WoodBox, CrystalCage S/M (spin or hip drop), HipDropRock (hip drop only); their coins pop
    out and bounce (CoinHolder::appearCoinPop);
  - CollapsePlane, SpaceMine, NeedlePlant, BlackHole / BlackHoleCube (miss), ElectricRail (shock unless squatting),
    FireBar (balls from the FireBarBall archive, converter `EXTRA` models);
  - InvisibleWall* collide without being drawn, CollisionBlocker pushes Mario out until its SW_B;
  - areas (areaobjinfo rows in OBJS): Switch Cube / Sphere / Cylinder (SwitchArea rules), Death* (a miss);
  - walking enemies (one table): Kuribo, TakoHei, PackunPetit, KoteBug, JumpSpider, Meramera; they pop a coin when
    they die, wait beyond 3000; RockCreator rolls rocks along its rail; NPCs (Kinopio, Tico, Penguin...) face Mario;
  - PicketSwitch / HipDropSwitch / HipDropTimerSwitch (hip drop);
  - rails are Bezier (converter samples curved parts, keeps the JMap points for point-based uses);
  - map parts and child objects (were missing): rotating parts (MapPartsRotator) and rail movers (MapPartsRailMover:
    point speed / stop, end types) move with their collision and carry Mario;
  - FlipPanel / FlipPanelReverse flip after 3 frames under Mario (turned over: ponytail for the colour animation),
    FlipPanelObserver turns SW_A on once all of its GroupId are flipped (rows carry GroupId in the old pad);
  - items taken on touch: PowerStar (counted, spins), 1-up / life-up mushrooms, QuestionCoin, Yellow / BlueChip
    (their ChipGroup turns SW_A on when complete); ItemBlocks (Timer / Splash Coin / Piece blocks, ItemBlockSwitch)
    hit from below give all their items at once;
  - EarthenPipe pairs (arg3), instant warp and the partner's exit jump; hidden pipes (arg2 1 / 2) rise when used;
  - stage switches: per-zone (< 1000) and global ids, appear switches hide / show rows (instances, coins, star pieces,
    launch stars), SW_DEAD on kills, Coin SW_B kills; logic rows TimerSwitch, SwitchSynchronizerReverse,
    GroupSwitchWatcher (`out/switch_census.json`: which classes use which switch); PunchBox / CrystalCage SW_A,
    Kuribo appear / dead;
  - blending: each VUMD group carries its material's blend (MAT3 blend mode: opaque / blended / additive, z write,
    material alpha); blended groups get their own GS state and are drawn after the opaque pass, farthest instance
    first. `NO_DRAW` is down to SphereAir (its glow is a TEV rim effect);
  - materials: the converter evaluates each material's TEV stages (`tools/tex.py` `tev`, `bake`) and bakes them into
    its texture; untextured groups take the TEV's constant colour. ponytail: no lighting (RAS = material colour),
    every stage at the base texture's UVs; near-white results fall back to the plain texture (Coin);
  - video: 640 x 224 buffers shown in FRAME mode (each field reads every line; layout and projection in 448-line
    units): the 448-line FIELD setup bobbed one line per frame in PCSX2 (each buffer always on the same field). Frees
    ~1.7 MB of GS memory (two colour buffers + z at half height) for the texture cache; `vram_need.py`'s 720 KB budget
    can grow;
  - lighting: lit materials (colour channel 0 lighting on: WoodBox, Kuribo...) get smooth per-vertex shading from
    their NRM normals (`shade`, half-Lambert, one fixed light in model space), unlit ones none; planets keep the
    vertex colours Nintendo baked (unlit, CLR0);
  - skies (Sky kind and VROrbit / VRDarkSpace / VRSandwichSun) drawn first at the camera, then `render_clear_depth`;
    projected sky textures get spherical UVs (`SKY_ARCS`);
  - music: `tools/music.py` (AST -> `out/music/*.pcm`), streamed through audsrv (`engine/src/audio.c`), looping;
    per scenario: `python tools/music.py --table data/fs/files engine/src/music_tab.h` ports Petari's stage BGM
    (AudStageBgmWrap: per stage, per scenario group, comets) for 132 scenarios / 31 streams, `music.py data/fs/files
    out/music` converts those (~300 MB); sequenced BGMs (LibraryRoom...) and the disc build fall back / stay silent;
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
| Modes | `plain`, `bench`, `slope`, `zone` (first SuperSpinDriver at frame 120), `box` (FlipPanelEx 1: the coin pops and is taken), `collapse`, `hazard` (EggStar 1: third NeedlePlant prick is a miss; BattleShip 2: mine, electric rail, black hole; Factory 1: FireBar), `item` (BattleShip 2; CosmosGarden 1: a QuestionCoin's dead switch brings star pieces), `spin` (BattleShip 2, CannonFleet 1), `pipe` (BattleShip 2), `hip` (Factory 1), `flip` (FlipPanelEx 1: 18 panels, the group completes) |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| Coverage (task 5.3) | `build.sh cover out/slice/hd2.bin BattleShipGalaxy/2` then `tools/coverage.ps1` → `out/coverage.csv` (one ELF, the scenario through `out/boot.txt`; ~1 min each); `python tools/class_coverage.py` → `out/class_coverage.md` (behaviour / static / missing placements) |
| Disc image | `build.sh MODE out/slice/hd2.bin Galaxy/n disc` (→ `out/disc/SLICE.ELF`) then `python tools/disc.py` → `out/smg-ps2.iso` (819 MB, all 136 scenarios' packs); boot with `tools/run_quiet.ps1 -Iso out/smg-ps2.iso`; a boot menu lists every scenario (D-pad, X; the build's scenario starts after 10 s) |
| PCSX2 in the background | `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, `tools/snapbg.ps1 <png>`, log `Documents/PCSX2/logs/emulog.txt` |

Patch scripts go through files (Write), not bash heredocs, because of apostrophes. In Python-generated C, write `\n` as `chr(92) + "n"`.
The OBJS row format (`ROW` in `tools/slice.py`, `Row` in `engine/src/blob.h`) changed to 92 B (switches): any change
there needs the embedded blob and every pack rebuilt.

## Next

0. Look: effects (sparkles, stars).
1. More switch sources and users: IronCannonLauncherPoint, StarPieceSpot, Kinopio / Tico; spawned power stars
   (FlipPanelObserver arg7, requestAppearPowerStar); RestartCube (checkpoints).
2. Lumas / NPCs (static models today).
3. More classes by placement count (`out/census.json`): GCaptureTarget, FlowerGroup, CutBushGroup, ShootingStar,
   Kinopio, FlipPanel; LavaSteam (its danger is an effect: needs effects first).
4. Vertex alpha in VUMD (rgbi has no room: the 4th byte is the index) for SphereAir-like fades.
5. StarPieceFlow, CrystalCageL, Karikari (clings to Mario: needs Mario-side slowdown), MechanicKoopaMini.
6. Task 5.3 gate: coverage run done once (136 / 136 play, `out/coverage.md`, `tools/coverage_report.py`), class
   weights (`out/class_coverage.md`); left: the final run on the current build, worst frame on the heaviest scenario,
   videos. Blocking zone loads (a zone wanted before its prefetch ends) are the worst frames (HoneyBee ~0.4 s).
7. Last (the user's call): sound effects (AFC -> ADPCM), music on the disc (streams in the ISO), ChangeBgmCube / RestartCube BGM changes.
