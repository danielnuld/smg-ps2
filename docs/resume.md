# Where phase 2 stands (resume notes)

Last updated 2026-10-09. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

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
  - coins: Coin, RailCoin, CircleCoinGroup and the purple ones, StarPiece, StarPieceGroup, StarPieceFlow (arg1 pieces
    riding its rail at arg0, round again); ShootingStar (appears arg2
    up its y, falls, bursts into arg0 of the arg3 star pieces it holds, again every arg1 frames until empty);
    FlowerGroup / FlowerBlueGroup / CutBushGroup hide arg1 coins (star pieces if arg2 is 1) in random members, out
    when Mario moves or spins through one;
  - breakables: PunchBox, WoodBox, CrystalCage S/M (spin or hip drop), HipDropRock (hip drop only); their coins pop
    out and bounce (CoinHolder::appearCoinPop);
  - CollapsePlane, SpaceMine, NeedlePlant, BlackHole / BlackHoleCube (miss), ElectricRail (shock unless squatting),
    FireBar (balls from the FireBarBall archive, converter `EXTRA` models); IronCannonLauncher(Point): every arg0
    frames a cannon shell (KoopaJrShipCannonShell) flies straight at arg1, explodes on walls or on Mario (hurt), a
    stomp drops it (SW_A gates the Point; no star pointer freeze); LavaSteam (Sign effect, then 90 frames of
    steam that burn, every 300 frames; SW_A starts it); LavaGeyser (Wait arg0, Sign, a column shooting up to its
    Top joint's height for arg1 frames that burns along its y; ponytail: the bind-pose model stretched, not the BCKs);
  - AssemblyBlock (PartsA / B / TimerA...): floats and turns at its home (the linked GeneralPos 合体ブロック故郷点,
    slice.py puts it in args 2-6), flies to its place in 10 frames when Mario comes within arg0, back when he leaves
    (arg7 0: vanishes arg1 frames after, blinking the last 100);
  - JumpStand: Mario landing on it is held 5 frames and thrown at 26 (32 with jump), a hip drop sinks him 15
    frames and throws him at 43 along its up; the model squashes (ponytail for its BCKs); its KCL sits at
    JumpStandJoint03 (`KCL_JOINT`, `kcl_shift`: it was 175 under the model);
  - WarpPod: pairs by GroupId in its zone; touching one takes Mario 160 up it in 60 frames, then on MarioWarp's arc
    (pi / 4, max(radius / 25, 120) frames) to 160 up the pair, which waits 60 frames (ponytail: all pods active, no
    event camera, no path drawing);
  - Tsukidashikun: on its rail from the nearest point; (SW_B,) Wait arg1, Sign 60, Move at arg0 to the end, back...,
    carrying Mario;
  - Kiraira: drifts along gravity, faces Mario within 1500, explodes on touch (hurt, SW_B on), down 480 frames
    then back (ponytail: no chain, no group alarm, no KirairaRail, SpaceMine's explosion);
  - MeteorStrike(Environment) launchers: every arg1 x 60 frames a meteor falls from rail point 0 towards point 1 at
    arg0, turning; breaks on the map, hits Mario (90); Environment ones touch nothing (ponytail: one at a time, no
    break model / lava column / shadow);
  - HomingKiller launchers: the bullet comes out (200 in 20 frames, shake, stop), flies level at 12, chases Mario
    within arg0 in front of it (turn cos 1 - arg1 x 1e-4) unless arg2; breaks on the map, on Mario (hurt), 3000 off
    or after 600 frames; the next one 180 frames later (ponytail: no freeze, no roll, no Torpedo / MagnumKiller);
  - star pointer stand-in: a cursor on the right stick (6-16 px a frame, R3 centres it, `cursor_ray` through the
    camera); star pieces shown within 24 px of it (and 4000 of the camera) are taken;
  - GCaptureTarget: R2 with the cursor on one (200 of its ray, within arg1 of Mario) captures it; 30 frames later
    Mario is pulled (0.7 x power, damped 0.99 / 0.75 near), R2 let go: he floats on (0.99) until 180 frames or the
    map, then falls free with that velocity; R2 on another recaptures, R1 or arg0 breaks (ponytail: no ribbon);
  - star piece shots: L1 spends a taken piece, thrown at 40 towards where the pointer's ray meets the map; walkers
    stagger 180 frames, Takobo stuns 240, MechanicKoopaMini goes SpinHit then fires, a free Karikari dies, a Kiraira
    explodes; HomingKiller and meteors just reflect it;
  - TreasureSpot / CoinFlower: a shot (Mario within 2000) pops their coin, the spot goes, the flower stays;
    HomingKiller: R2 pointing at the bullet freezes it (20 frames, renewed while pointed);
  - LavaProminence: every arg0 frames (Sign 90) a lava ball comes in along its rail's start, rides it at arg1 and
    goes out past the end; its body (120) burns;
  - SmallStone / CircleShell / CircleStrawberry: 8 members on a 300 ring dropped onto the ground (plant members); a
    spin within 400 breaks them all (a star piece each with arg2); groups not placed at their rebind retry near Mario;
  - InvisibleWall* collide without being drawn, CollisionBlocker pushes Mario out until its SW_B;
  - areas (areaobjinfo rows in OBJS): Switch Cube / Sphere / Cylinder (SwitchArea rules), Death* (a miss),
    RestartCube (checkpoints: a miss brings Mario back to the start point of its zone with MarioNo arg0; STRT rows
    carry MarioNo in arg0); StarPieceSpot (logic row: arg0 star pieces, default 5, burst out on its appear switch, or
    at a spin when it has none; ponytail: dropped in a ring, not thrown);
  - walking enemies (one table): Kuribo, TakoHei, PackunPetit, KoteBug, JumpSpider, Meramera, Takobo (patrols its
    X / Z axis +-arg3 eased, punches Mario within 250, pressed by a stomp, blown by a spin), MechanicKoopaMini (hops at
    Mario within 900, pursues, fires 120 frames within 300; a stomp or spin stuns it, a hip drop breaks it; ponytail:
    waits instead of walking its rail), Karikari (hops at Mario within 1000, clings (5 at most): his jumps fall (2 + n) / 2
    faster, a life every 120 frames down to 1; a hip drop throws them off, a spin kills a free one), KuriboMini (a Kuribo a spin
    kills), Karon (pursues within 700; a stomp, hip drop or spin breaks it for 300 frames, then it rises again); they pop a coin when
    they die, wait beyond 3000; RockCreator rolls rocks along its rail; NPCs (Kinopio, Tico, Penguin...) face Mario;
  - PicketSwitch / HipDropSwitch / HipDropTimerSwitch (hip drop);
  - rails are Bezier (converter samples curved parts, keeps the JMap points for point-based uses);
  - map parts and child objects (were missing): rotating parts (MapPartsRotator) and rail movers (MapPartsRailMover:
    point speed / stop, end types) move with their collision and carry Mario;
  - FlipPanel / FlipPanelReverse flip after 3 frames under Mario (turned over: ponytail for the colour animation),
    FlipPanelObserver turns SW_A on once all of its GroupId are flipped (rows carry GroupId in the old pad);
  - event power stars: `slice.py` `declare_stars` resolves which star each declarer brings out (ScenarioData
    AppearPowerStarObj + ObjNameTable, PowerStarEventKeeper::findStarID) and puts the declarer's OBJS index in the
    star's arg7 (-2: a hundred coins). The engine (`star_wait`, `star_pos`) hides the star (or shows it inside its
    cage) until the declarer is done, then plays the appear demo: 300 up, an arc to its place, taken from frame 210
    (no demo camera). Declarers: FlipPanelObserver, CrystalCageL (2 spins 20 frames apart), BreakableCage(Rotate / L)
    with a star inside (ponytail: a spin breaks them), 100 coins (HeavensDoor 2);
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
  - mipmaps: PSMT4 textures carry up to 3 levels (box-filtered; one 16-colour palette quantized from the base and
    its mips, level k weighted 2^k, so the mips keep in-between tones: `tex.to_ps2`; confirmed by the user), the GS picks
    them by depth: K per texture from its texel density (`slice.lod_k`) plus `LOD_BIAS` (2 levels: surfaces seen
    edge-on), trilinear; rotating discs in BattleShip shimmer about half as much. 136 / 136 fit the 2416 KB cache;
  - particle effects (`tools/jpa.py`, `engine/src/fx.c`): a lean JParticle port (emitters, volumes, velocities,
    gravity fields, scale / alpha curves, colour keys; TEV colour combine and ETX1 second texture baked into 32 px
    textures, billboards via `render_sprites`) for coin / star piece / chip gets, box / cage breaks, mine explosions,
    enemy deaths, and the lights of star pieces, power stars, launch stars, ? coins and chips (`FX` list, PTCL in the
    resident pack); host test `test_fx`;
  - static models leave out shapes on joints scaled to 0 at rest (`slice.hidden_joints`), and `Name#BCK` extra
    models take a BCK's last-frame visibility: FlipPanel shows "?", "!" once turned, End when its group is done
    (its three faces overlapped: the mosaic the user saw); textures take their real GS block footprint (page
    swizzle: a 128 x 32 PSMT4 spans 12 blocks, not 8); baked TEV stages on other coordinates than the base (env
    maps, sparkles) use their texture's mean colour; one LOD K per texture over every group using it;
  - static models are posed: each vertex goes through its DRW1 draw matrix at the bind pose (Maya scale
    compensation in `skin.world`); rigid shapes keep joint-local positions, so 553 models drew their parts at the
    origin (LavaGeyser's rings in one disc). Those models skip simplification (it runs before posing);
  - KCL byte-swap (`kcl_to_le`) fixed: a prism list's first u16 (never read) can be the low half of an octree
    word, and lists overlap; 605 of the 1016 KCLs had corrupt words or lists (AssemblyBlockPartsB crashed the octree walk);
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
| Modes | `plain`, `bench`, `slope`, `zone` (first SuperSpinDriver at frame 120), `box` (FlipPanelEx 1: the coin pops and is taken), `collapse`, `hazard` (EggStar 1: third NeedlePlant prick is a miss; BattleShip 2: mine, electric rail, black hole; Factory 1: FireBar), `item` (BattleShip 2; CosmosGarden 1: a QuestionCoin's dead switch brings star pieces), `spin` (BattleShip 2, CannonFleet 1), `pipe` (BattleShip 2), `hip` (Factory 1), `flip` (FlipPanelEx 1: 18 panels, the group completes, its power star comes out and is taken), `cage` (CosmosGarden 3, SandClock 1: two spins at the CrystalCageL, then its star), `restart` (BattleShip 2: a miss after each of two RestartCubes), `spot` (BattleShip 1: cages whose dead switch bursts a StarPieceSpot), `cannon` (CocoonEx 1: Mario in a launcher's line of fire), `shoot` (StarDust 1: ShootingStar bursts), `plant` (EggStar 1: Mario through every member of the plant groups with items), `steam` (IceVolcano 2: Mario in a LavaSteam vent), `geyser` (IceVolcano 2: Mario in a LavaGeyser column), `assembly` (StarDust 1: Mario over 20 AssemblyBlock places), `jumpstand` (Factory 1: dropped on a JumpStand, then in a hip drop), `warppod` (HeavensDoor 2: into a WarpPod twice), `tsuki` (IceVolcano 1: Mario on a Tsukidashikun), `kiraira` (OceanPhantomCave 1: Mario into a Kiraira every second), `takobo` (EggStar 1: a Takobo punches, a stomp, a spin), `meteor` (FloaterOtaKing 1: Mario under a MeteorStrike; the test shows them without their boss switch; `TEST_SHOWN` in game.c), `killer` (BattleShip 1: Mario 1500 in front of a HomingKiller), `mkoopa` (Factory 1: a MechanicKoopaMini fires, a stomp, a hip drop), `karikari` (EggStar 1: Karikari cling, bite, a hip drop throws them off), `flow` (CocoonEx 1: Mario takes moving StarPieceFlow pieces), `cursor` (CocoonEx 1: the pointer sweeps the screen taking star pieces), `gcap` (StarDust 1: R2 on a GCaptureTarget pulls Mario, then he floats and falls), `shoot2` (EggStar 1: a star piece shot stuns a Takobo, a stomp), `tspot` (BattleShip 2: shots open TreasureSpots), `kmini` (BattleShip 1: KuriboMini stomped, spun), `karon` (SandClock 1: a Karon broken and back), `prom` (HellProminence 1: Mario on a LavaProminence's rail), `stone` (CubeBubbleExLv1 1: spins break SmallStone rings); `killer` also freezes a bullet with R2 |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| Coverage (task 5.3) | `build.sh cover out/slice/hd2.bin BattleShipGalaxy/2` then `tools/coverage.ps1` → `out/coverage.csv` (one ELF, the scenario through `out/boot.txt`; ~1 min each); `python tools/class_coverage.py` → `out/class_coverage.md` (behaviour / static / missing placements) |
| Disc image | `build.sh MODE out/slice/hd2.bin Galaxy/n disc` (→ `out/disc/SLICE.ELF`) then `python tools/disc.py` → `out/smg-ps2.iso` (819 MB, all 136 scenarios' packs); boot with `tools/run_quiet.ps1 -Iso out/smg-ps2.iso`; a boot menu lists every scenario (D-pad, X; the build's scenario starts after 10 s) |
| PCSX2 in the background | `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, `tools/snapbg.ps1 <png>`, log `Documents/PCSX2/logs/emulog.txt` |

Patch scripts go through files (Write), not bash heredocs, because of apostrophes. In Python-generated C, write `\n` as `chr(92) + "n"`.
The OBJS row format (`ROW` in `tools/slice.py`, `Row` in `engine/src/blob.h`) changed to 92 B (switches): any change
there needs the embedded blob and every pack rebuilt.

## Next

0. Star pointer on the right stick (agreed with the user): cursor, GCaptureTarget, shots, TreasureSpot, freeze done;
   left: pointer effect sprite, railed GCaptureTargets (objinfo rows: no MapPartsRailMover columns).
1. Look: more effects (child particles, rotation, other field / shape types; Mario's dust, on his joints).
   Mario has SpinLight, HipDropFlower and Damage (`mario_fx`).
2. More switch sources and users: Kinopio / Tico.
   More power star declarers (`STAR_DECLARERS` in slice.py, then the origin in `star_pos`): bosses, Tico
   collecting (チコ集め), Kuribo / SkullShark baby "all dead" checkers, NPC events; PowerStarAppearPoint groups.
3. Lumas / NPCs (static models today).
4. More classes by placement count (`out/census.json`, `out/class_coverage.md` top static): GCaptureTarget (needs a
   pointer stand-in), WaterPlant, BeeFlowerHover, Kinopio (TreasureSpot
   needs the star pointer).
5. Vertex alpha in VUMD (rgbi has no room: the 4th byte is the index) for SphereAir-like fades.
6. Bug seen: CosmosGarden 1 / 6, PuffballZone (rows near the origin) goes back to its low pack right after its full
   one loads with Mario standing there (no collision under its SmallStone). Likely cause: streaming keeps one zone
   chain full (`chain_of` in main.c, the zone Mario is judged to be in); near the origin several zone spheres overlap and
   another one wins. A fix: several full chains at once within the budget (every zone whose sphere holds Mario).
7. Task 5.3 gate: coverage run done once (136 / 136 play, `out/coverage.md`, `tools/coverage_report.py`), class
   weights (`out/class_coverage.md`); left: the final run on the current build, worst frame on the heaviest scenario,
   videos. Blocking zone loads (a zone wanted before its prefetch ends) are the worst frames (HoneyBee ~0.4 s).
8. Last (the user's call): sound effects (AFC -> ADPCM), music on the disc (streams in the ISO), ChangeBgmCube / RestartCube BGM changes.
