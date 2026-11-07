# Where phase 2 stands (resume notes)

Last updated 2026-10-09. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

## Parallel agents (2026-10-28 / 29): read this first

Four agents in worktrees D:/smg-ps2-wt/{perf,release,goodegg,gaps} (branches agent-*; D: is exFAT, no junctions: they
build with their own build.sh, run ELFs copied to D:/smg-ps2/out/wt-<name>-<mode>.elf, take PCSX2 in turns through
`mkdir D:/smg-ps2-wt/pcsx2.lock`; only the release agent writes out/packs). From Git Bash call WSL as
`MSYS_NO_PATHCONV=1 wsl.exe -e bash /mnt/d/...` (else the /mnt path is rewritten: E_INVALIDARG). Merged:
- #9 (issue #3) EE performance: Good Egg bench, EE time a frame (render_begin's flip / VIF / clear wait now printed
  apart: ~3.4 ms of the old "draw" was that wait): start view 19.9 -> 12.5 ms (logic 6.7 -> 5.1, draw 12.7 -> 7.0),
  path 16.4 -> 11.5, worst 38-42 -> 26. Clip cache sized per clip (inflates 8300-15000 -> 7-22 a bench), skinned
  batches written straight into the VIF chain (emit_batch, checked equal to packet2's), OceanSphere off outside the
  view, clip-name hash, NDEBUG, pickups' one sin / cos, bloom / area rows listed once, kcl_check_sphere early out.
  route EggStar 1 identical (star at frame 3781). Tried, no gain: ocean GS registers precomputed, lazy projection.
  Ideas left: Mario's anim_skin several times a frame, plants (CutBush / FlowerGroup ~1.3 ms), update_mario ~1 ms,
  launched pieces, items, ocean spheres on screen (~2 ms). Second round in progress.
- #7 (issue #6) star return demo: MarioHair and Cap0 hidden in ResultWaitStart (21-221), DemoFlyLightDemo at step 30
  (needs AstroDome / AstroGalaxy repacked: slice.py MarioHair extra), the Butler lecture's pointing SE. Skipped:
  PointerGuidance / StarPieceCounter TicoEat mode, CinemaFont26 (two text fonts in subs.bin), camera interpolation
  back after demos, BGM fade-in. Seen: the result window stops the whole game update (Mario's anim too); Petari keeps
  the scene running.
- Sound agent (branch agent-sound2, PR: see the issue "ME sequencer and missing streams"): the seven streams bgm_tab
  named but out/music lacked (title, ev_prolo01 / 01_b / 04, ev_kuppabt01, first_astro, second_astro) converted;
  music.py now writes AST loop flag 0 as a one-shot (staffroll, prolo04, first / second_astro). The ME sequencer
  (engine/src/me.c after RhythmLib; me_has / me_start_system = MR::hasME / startSystemME; notes through se_seq.c's
  se_note), its rhythm from tools/rhythm.py's <stream>.rhy (each rhythm BGM's sequence run offline: beat callbacks,
  chord / scale against the stream's samples; audio.c loads it with the music). sound.py --pack carries JaiMe.arc
  (SNDB header 80 bytes: [15] metable, [16] defaultme; older packs: no ME) and the MEs' waves. Host test test_me.c.
  **Release agent, to copy:** out/wt-sound/music/* (the 7 new .pcm, SMG_staffroll_strm.pcm rewritten as a one-shot,
  74 .rhy) into out/packs/music; then every sound pack rebuilt with this branch's tools/sound.py (out/sound_all.sh,
  and the shared out/packs/sound.bin) so they carry the ME data and waves; tools/disc.py now packs .rhy.
  Dev runs: XF=-DPACKS_OVERLAY=wt-sound also reads music/ from the overlay (audio.c m_open).
- #8 (issue #5, partial) Good Egg rim: tools/dolphin/trace.py (Mario every frame in Dolphin, frames labelled by his
  timers _3CE / _3BC), _41C starts at 0 and slip_keep's misuses replaced by Petari's conditions, tryDrop's _8F8 branch.
  The Wii at g 130 takes a 60.7 deg face (checkGroundOnSlope), slides g 131-132, drops g 133, lands g 142; the port
  takes the 58.7 deg one 14 higher and walks on. In progress: checkGround / checkGroundOnSlope (patch
  D:/smg-ps2-wt/goodegg-checkground-wip.patch), Petari's binder 60 (the port's sphere is 50 at 50), the shadow pull.
- gaps8 (branch agent-gaps8, from fix/host-tests): TicoStarRing talk (its galaxy name as the talk's string
  argument, TalkDirector's yes / no node: A answers Yes, Meta on Yes), KoopaStatue (explosions break it:
  KoopaStatueBreak's clip, left on its Break BVA), Hanachan (5 parts, rail walk, trample / angry pursue / overturn /
  pressed), StringSpider (walker kit WALK_STRSPIDER: its string and swing), BegomanSpike (walker kit WALK_BEGOSPIKE
  with BegomanBase's nerves, the attack permitter, electric rails), SpiderMapBlock (hip drop: its coin to velocity),
  SpiderCoin (R2 + cursor frees it; its SpaceCocoon). Test modes: `talk` with XF=-DTALK_CLS=7 (AstroGalaxy/1),
  `statue` (KoopaBattleVs3Galaxy/1), `hanachan` / `strspider` (KoopaJrShipLv1Galaxy/1), `begoman`
  (BattleShipGalaxy/3), `spiderblock` (OceanPhantomCaveGalaxy/1), `spidercoin` (StarDustGalaxy/3); the new tests
  count frames from Mario free (game_test_free). Overlay packs: out/wt-gaps3. All seen working in PCSX2 but
  spiderblock (GhostShipCaveZone never streams in there: the same zone-room limit as below) and SpiderCoin's pointer
  (StarDust 3's boss camera keeps it off screen; the test frees them directly).
  The repack must pick up (tools/slice.py, tools/msg.py): msg.py DIRECT TicoStarRing (AstroGalaxy);
  EXTRA KoopaStatue ("#Break", "KoopaStatueBreak"), Hanachan (HanachanHead / Body / BodyS), StringSpider
  (StringSpiderString), BegomanSpike (BegomanSpikeHead, BegomanBrokenPiece), SpiderCoin (SpaceCocoon); SKINNED_OBJ
  KoopaStatueBreak, BegomanBrokenPiece, HanachanHead / Body / BodyS; BRK_POSE the Hanachan parts; NO_DRAW + LOGIC
  Hanachan. Galaxies: AstroGalaxy, KoopaBattleVs3Galaxy, KoopaJrShipLv1Galaxy, BattleShipGalaxy, OceanPhantomCave,
  StarDust, and every other one with these classes (Hanachan: HeavenlyBeach, FlowerPlanet, EringiPlanet, ForestHome,
  WormEaten, Poihana, ReverseForestHome zones; StringSpider: OceanRing, PhantomNest, TeresaHouse; KoopaStatue:
  PhantomNest, TeresaHouse; BegomanSpike: CannonFleet).
  SpiderThread (#198): the SpiderCoins bind to TombSpider's web (see TombSpider below).
- agent-perf3 (EE time, third round; on fix/host-tests): the sampling profiler works in PCSX2 now (`XF=-DSAMPLE_PROF`,
  `-DPROF_FROM= -DPROF_TO=` for a route's stretch; `python3 tools/prof_map.py <emulog> <nm -n of the ELF>`: self time
  by function and strcmp / memset / sin / cos / snprintf callers) and the route test prints the EE's time each 200
  frames (`route perf f N: logic, draw without the waits, worst`). Exact changes only (same functions of the same
  inputs; bench end states and the route's log line for line unchanged, star at 3781), except the drawings' angles
  that turn with the frame (taken within one turn). Good Egg bench, start view: logic 4205 -> 3026 us, EE draw 7139 ->
  5771; path: logic 4112 -> 2968, EE draw 5623 -> 4630. Route: logic 7758 -> 4816 at f 600, 6644 -> 4407 at f 3200,
  worst frames 92 -> 30 ms (f 1400), 88 -> 16 (f 3000). What it was: names looked up by strcmp walks every frame
  (area classes, scene_type / scene_model, Mario's anim names, rows by class; at a rebind once a row), sines / cosines
  of unchanging rotations (areas, camera areas, joints) and of huge angles (newlib's slow reduction), memset for
  small matrices, gravity_at over every gravity (now a grid), skinning on the FPU (now VU0). Ideas left: KCL
  (kcl_check_sphere 0.5-0.8 ms a frame in heavy places; kcl_each walks every KCL instance a query, ~0.35 ms: moving
  parts keep a grid from being exact), draw_model_ ~1 ms (skinned batches), particles fx_draw + render_sprites +
  fx_update ~1 ms (on the EE), Mario's pose ~0.36 ms (30 joints, 4-track blend), walkers posed one by one, zone swaps
  and rebinds (game_rebind on a kill / switch: the whole instance table again, 2.8-11 ms a call, `route perf`'s
  rebinds; a class's flags made once per type would cut most), fx_anim's strcmp search a skinned draw with effects,
  se_update's powf / sinf a channel a frame. Bench builds only: render_text's debug lines (~0.6 ms) and PCSX2's host reads (fread walks every
  FILE: _fwalk_sglue) inflate the numbers. No pack format change.
- perf, Petari clipping (PR #63 on): obj_clipped ports ClippingActorInfo per class (Tico / LuigiNPC default sphere,
  SuperSpinDriver). For the release agent: the pack's rows lack ClippingGroupId (SHORT, -1 none) and ViewGroupId; with
  them ItemBlock (ItemBlock.cpp:72 setGroupClipping) and the ViewGroupCtrlCube far level could clip as Petari does.
- zones (branch agent-zones): every zone streams in when Mario needs it. main.c: the room for zones he needs is the
  region's free bytes (the low packs' sum was subtracted as a swap reserve and then counted again in the set: 4503
  instead of 6510 KB in PhantomGalaxy/1); the nearest needed zone not yet in is the one always taken (it used to be the
  main stage he stands on, so a second zone came only "if it fit"); the zones he only heads for keep the old reserve
  (zone_room_bg: no load pause for a maybe); a full zone stays full while it fits (leaving the house no longer
  reloads); when the kept packs split the free bytes, pend_evict_all frees every stage pack and reads the set again in
  one piece. Load lines: `packs: <scen>, n zones (room / in the background)`, `packs: zone X never fits` (it then
  stays low, no hang). tools/zone_fit.py lists offline every zone of out/packs that can not fit (slice_all.room).
  slice_all.py: ROOM 9.1e6 (measured 9.18e6 with the test ELFs' 13824 KB region) and room() less letter.bin (loaded
  in every galaxy while Luigi is missing). Checked in PCSX2: torch (PhantomGalaxy/1: TeresaHouseZone in at the first
  torch, coin 84 popped and taken, coins 1), spiderblock (OceanPhantomCave/1: GhostShipCaveZone in, 7094 of 7150 KB;
  SpiderMapBlock found). Still wrong in spiderblock (not zones): the hip drop lands on GhostShipCavePlanet at y -163,
  122 over the block's origin, so the block is not hit; Mario then stays in HipDropLand (m_proc_hip no longer runs).
  **Repack must redo** with this branch's slice_all.py: AstroGalaxy (all), OceanPhantomCaveGalaxy, PhantomGalaxy (and
  then `python tools/zone_fit.py out/packs` must list none): with the old packs 13 zones are over the new room
  (letter.bin aside): AstroGalaxy 1-5 (main and TridentZone), OceanPhantomCave 1-2 GhostShipCaveZone, PhantomGalaxy 1
  TeresaHouseZone. slice_all's shrink steps now go on to 0.6 / 0.5; the trial repack in out/wt-zones (overlay, those 3
  galaxies) fits all 76 zones (tightest PhantomGalaxy/1 TeresaHouseZone, 119 KB left; OceanPhantomCave/1 now 6541 of
  7155 KB) and AstroGalaxy 1-5 / OceanPhantomCave 1 went to a smaller ratio (simpler static models in the Observatory:
  look at it after the repack). engine/test/run.sh now tests the tree it lives in and strips the SRCS lines' CR.
- Release repack (agent-release2, 2026-10-30 / 31): out/packs backed up to out/packs-backup-2026-10-30 (all but music).
  With main's tools at cf026b7: lyt.py (menus.bin 598 KB with IconAButton / PointerGuidance, subs.bin 143 KB with
  CinemaFont26), slice_all.py (resident.bin 3.99 MB with the torch effects, resident_luigi.bin, all 136 scenarios;
  AstroGalaxy / OceanPhantomCave / PhantomGalaxy again with #35's room: `zone_fit.py out/packs` lists none),
  out/wt-sound/music/* copied into out/packs/music (7 .pcm, staffroll one-shot, 74 .rhy), every sound pack and the
  shared sound.bin rebuilt with main's sound.py (ME data). Logs: out/release_*.log.
  Coverage (tools/coverage.ps1 -Elf out/wt-release-cover.elf, one galaxy per PCSX2 lock; failed runs' logs kept in
  out/cover_logs): 115 / 123 ok (EpilogueDemoStage now left out: only reached with its demo sequence, `ending` test).
  Not ok: CannonFleet 1-5 hung in game_init after the "talk:" line (fixed, fix/cannonfleet-hang: not #24 but #33's
  gravity grid, whose cell count of a range 1e8 gravity overflowed the EE's 32-bit long; now 120 / 123 ok, CannonFleet
  1-5 boot and play to the cover line, route EggStar 2 star at f 3661); KoopaJrShipLv1 1 (Mario lands at
  the start, (-28500 538 -28200), and falls through to BlackHoleCube 38 in plain play too: the PhantomShipBridge row
  127 draws raised; its Move part's matrix was seen not following the joint's MoveA end pose; open); PhantomGalaxy 6 /
  BattleShip 1 / CannonFleet 1 (before the hang) are the cover test's landing (a black hole, a JumpBeamer's rings, a
  DeathCube edge at -7892 -8066 -237), not port bugs found. Fixed on the way (this branch): the scene logic array
  counted without FallsCube (HeavenlyBeach 1-5, HoneyBeeKingdom 3 / 5 crashed at boot), the death area line prints
  Mario's position. Seen and handed on: EggStar 1's own sound pack (10 MEs) crashed at boot (jump to 0 once
  SMG_galaxy01_strm.rhy loads; its ME table zeroed: fine) on the #30 engine; with main's engine and the final packs it
  boots and routes. The pack region is now 13568 KB with main's ELF (13824 when ROOM 9.1e6 was measured): the
  tightest zones (PhantomGalaxy 1 TeresaHouseZone 119 KB left) still loaded in the coverage run, keep an eye on it.
  Route EggStar 1-4 on main's engine and the final packs: 2, 3, 4 get their star; 1 crashes at boot with the route
  ELF (the same ME jump to 0: it depends on the ELF's layout, the cover ELF booted it), with its ME table zeroed it
  routes to the star at f 3781 as before. Release disc: build.sh plain ... FileSelect/1 disc, then main's disc.py
  (takes the .rhy) -> out/smg-ps2.iso.
- Pack region for the disc (release/astro-fit, 2026-10-31): #56 found the disc ELF (16879 KB end, 13312 KB region)
  left AstroGalaxy 1-5's main stage 134-232 KB over its room. Cause: every build linked out/slice/hd2.bin (6 MB of
  .rodata, slice_blob) though with packs (Galaxy/n, disc) main.c never reads it. build.sh now links an empty blob when
  given Galaxy/n: disc ELF ends at 10932 KB, region 19200 KB (zone_fit: all 906 zones fit, tightest AstroGalaxy/1
  TridentZone 5540 KB left); host packs ELFs 10732 KB -> 19456 KB measured in PCSX2 (zone_fit predicts the same). No
  repack: packs unchanged (still sliced to ROOM 9.1e6 / main.c PACK_ROOM, now the streaming bound, not the region).
  Checked from out/packs: AstroGalaxy 1, PhantomGalaxy 1, OceanPhantomCave 4 boot, no "never fits" / "no memory",
  59.9 fps; route EggStar 1 star at f 3781. Old ELF / ISO in out/bak-astro-fit.
- Disc build in PCSX2 (fix/prologue-freeze, 2026-10-31): PCSX2 takes an image over 452849 sectors (~884 MB) for a DVD,
  and with its real BIOS no file of our DVD is found, by the OSD ("open fail name SYSTEM.CNF;1" with -slowboot), by
  loadelf ("open fail SMGP_000.01;1" with fastboot) or by the game's own sceCdSearchFile ("packs: no PACKS.TOC" with
  -elf; disc type 0x14). Tried without effect: full BIOS boot, a two-sector root directory (PCSX2's image type then
  DVD), a UDF bridge (mkisofs -udf). A real PS2 DVD's mastering / mechacon key is likely what it wants. Under the CD
  size the same image is a PS2 CD (type 0x12) and everything reads. So: a CD-sized test disc with only the scenes
  wanted, built with main's disc.py into out/wt-disc, e.g. packs = the root .bin, FileSelect, PeachCastleGardenGalaxy,
  HeavensDoorGalaxy, movies/Prologue*, music/SMG_{ev_prolo,fileselect,title,galaxy24}* (330 MB), then
  `tools/run_quiet.ps1 -Elf <disc ELF of any test mode> -Iso <test.iso>` (new: with both, that ELF runs with the image
  as its disc, no ISO rebuild per test). Checked so: `title` (A+B, new file -> PeachCastleGardenGalaxy/1, prologue
  stream from the disc), `prologue` with XF=-DCATCHUP and EECycleRate -3 (PrologueA and PrologueB play through from
  the disc, with subtitles and rumble). The console's prologue freeze did not reproduce. Made robust since, as the
  likely console-only cause (Neutrino reads the ISO over udpfs): a sceCdRead refused or ending in sceCdGetError was
  taken as read (the movie then played a stale chunk, libmpeg lost the stream); now asked again and logged ("cd:"),
  a chunk that is not one ends the movie ("movie: chunk n bad"), and "movie: <name> frame n" every 600 frames says
  on the console's udptty log where it stops.
- gaps (2026-11-04, worktree D:/smg-ps2-wt/gaps, overlay out/wt-gaps): #143's placed classes with no behaviour.
  - Fountain / SpringJetWater (#331, game.c update_jet_waters): Fountain.cpp (its effect, SE_OJ_LV_FOUNTAIN, SW_A
    Wait / Move, SW_APPEAR) and SpringJetWater.cpp (Wait / Jet / End on DemoSpringWaterRiseUp part 1 湧き水上昇, kill
    at 60, setStageBGMState(1, 180)); both ACTMES_FOUNTAINJUMP (sphere 70 at 350 up or the 280 cylinder: MarioFlow).
    Test XF=-DJWATER_TEST: CosmosGardenGalaxy/1 (Fountain row 200: MarioFlow each time), CannonFleetGalaxy/1 (its
    demo requested: Jet at part 1, End 240 frames later, killed at 60). slice.py LOGIC, jpa.py NAMED for both.
    CoinSpot needs nothing: in Petari nothing sets mIsInTornado (CoinSpot.cpp), it only waits.
  - FluffWind (#328, game.c fluff_wind_*): FluffWind.cpp's effects along its rail (one every 600, kept: forever
    emitters) and FluffWindHolder::calcWindInfo feeding Fluff::updateWind (push along the rail, side speed 6, front and
    up lean into the wind, RideFlyOnWind / RideSpinUpOnWind, initAfterPlacement facing the wind; Fluff's SEs, Splash /
    Blur / HardWind effects, SW_A). Test `fluff` CosmosGardenGalaxy/1 (now timed from Mario's first free frame): takes
    hold, on the wind (45), spin up on the wind, carried ~5000 along the rail, lets go on the map. slice.py LOGIC
    FluffWind, jpa.py NAMED Fluff / FluffWind.
  - Rabbit arg1 2 / 3 lectures (game.c rabbit_lecture, #242): SpinLecture / JumpSpinLectureMove every 180 frames of
    Talk outside a long talk; seen HeavenlyBeachGalaxy/1. arg1 0 (rail escape) left out: no placement uses it.
  - SwimTurtle Penguin's shell (game.c penguin_turtle, RemovableTurtle): Koura~0 on its back, Mario takes it (touch or
    spin within 450) as a JetTurtle (Inst.jet 3), back on it once that is gone. slice.py EXTRA Penguin Koura~0.
    Test XF="-DGAP_TEST=22 -DDEBUG_HUD -UCATCHUP" HeavenlyBeachGalaxy/3.
  - NokonokoLand (round 2; game.c noko_update, walker WALK_NOKO): the Koopa Troopa on its rail after Petari (walk /
    look around / turn, attack, rattle on a star piece, shell by trample / hip drop / spin, ShellWait 180, its shell
    becomes its JetTurtle (an extra Inst, Inst.jet 2, Koura~0 / ~1 by arg0) Mario holds and throws, FlyAway / Dead,
    reappears 350 away). slice.py: SKINNED_OBJ + BRK_ARG NokonokoLand, EXTRA Koura~0 / Koura~1 (MODEL_ARC
    "<arc>~<brk>:<frame>"), BRK_ARG names -1 args ~0 (args are unsigned). Test XF="-DGAP_TEST=20 -DDEBUG_HUD -UCATCHUP"
    KoopaJrShipLv1Galaxy/1 (trample, shell taken, thrown, back) and OceanRingGalaxy/1 (the red one walking).
  - #166 PurpleCoinCounter (game.c pc_update, hud_pc): made with the scene's purple coins or its PurpleCoinStarter,
    valid at once or at the starter's SW_APPEAR, Appear / Wait, one up every 3 frames, killed and back with the
    default layout. Repack: resident.bin (layout.py LAYOUTS + PurpleCoinCounter, in the FONT chunk's LAYO) and
    HeavensDoorGalaxy (slice.py LOGIC + PurpleCoinStarter). Seen: `purple` (EggStar 5, "x093 /100"), `talk`
    HeavensDoorGalaxy/2 with XF=-DPC_TEST (the starter's switch forced at frame 300: valid, Appear, Wait).
  - #170 ShockWaveGenerator / CapsuleCage (game.c update_gaps: Inst.gap = GAP_*, nerves in row_turn / row_step, the
    rest in GapSt by row, kept across rebinds). Test: plain build with XF="-DGAP_TEST=<GAP_ kind> -DDEBUG_HUD
    -UCATCHUP" (A tapped through the opening; Mario put by each placement in turn: 1 spins at the generators, 2 turns
    the cages' SW_B on). Seen in HeavensDoorGalaxy/1: the shock wave demo and its camera, a Kuribo staggered; both
    cages open with their camera and go. Repack HeavensDoorGalaxy (jpa.py NAMED ShockWaveGenerator: its ShockWave
    effect; slice.py SKINNED_OBJ CapsuleCage: its Move clip).
  - #178 DriftWood (the ice spikes drifting on HeavenlyBeach's ColdTwinFallZone rails: 3 a frame, turning, they flip
    Mario out of water) and Shellfish (Coin / YellowChip: the open / close cycle, its item shown and taken, the lid's
    hit and Hi collision on the Lid joint, Low still beside it, a star piece's shock). slice.py: COLL_JOINT (the joint
    a MapObjActor's own collision follows, SKIN pad 1), KCL_PARTS "<class>#Static" parts (game.c: an invisible
    instance at its owner), EXTRA ShellfishYellowChip YellowChip; jpa.py NAMED DriftWood (Ripple). GAP_TEST 3 / 4 / 5
    (HeavenlyBeachGalaxy 1 / 2 / 1): a flip off a drifting spike; the coin and the chip taken from an open shell.
    Repack HeavenlyBeachGalaxy.
  - #190 Battlerock: BattleShipElevator (Wait till Mario lands on it, the エレベーター上昇 time keep demo, its rail
    mover then, start / move / stop SEs), BreakableTrash (BreakableCage CAGE_TRASH: an explosion breaks it,
    BreakableCageBreak's Break clip, SW_DEAD), UFOStrongA (the generic rotor / railer already moved it: now its move SE,
    the isDone rumble, shake and end SE, its MoveLimit collision as a "#Static" part following it). GAP_TEST 6
    (BattleShipGalaxy/3: Mario put in the elevator, its demo, up the rail), 99 (BattleShipGalaxy/2: each trash broken
    by an explosion). Route EggStar 1: star. Repack BattleShipGalaxy.
  - #201 UFOKinoko (its rail mover waits for SW_B or its demo's ＵＦＯキノコ登場 action; move SEs) and CoinBox (a hidden
    coin a spin within 600 brings out: CoinGet, then the coin at 30 up / 10 across). The #143 classes' nerves moved
    into GapSt (row_turn / row_step are the rotors' / railers': UFOKinoko is a rotor too). GAP_TEST 7 / 8
    (StarDustGalaxy/2). Left: PlanetRocket (a PlanetMap playing its own-name clip: slice.py AUTO_SKINNED leaves planets
    out, so no planet animates). Repack StarDustGalaxy (LOGIC CoinBox).
  - #208 Penguin (game.c penguin_wait): arg0 3 dives (SwimDive, S_DIVE) every 120-300 steps, arg0 4's jet sound, the
    Default / Stand / Dash ones' talk action Talk / TalkA / TalkB at random each loop. Seen: HeavenlyBeachGalaxy/1's
    diver every few seconds. Left: SwimTurtle's shell (RemovableTurtle), PenguinCoach's golden shell talk / star,
    PenguinStudent's school (talk branches and TakeOutStar not ported).
  - #218 Caretaker's actions (arg0 -1 / 1 BWaitStand, else Wait / TalkNormal), BRK_ARG Penguin (ColorChange arg7) and
    Tico (arg0): their colour models "<class>~<k>" (game_rebind_'s table). Caretaker's BodyColor BRK bakes to one colour
    (the registers it drives are not baked), its tidy-up mission is not ported. Repack: every galaxy with Penguins or
    Ticos (full repack).
  - NPC rails (round 3; game.c npc_rail_move, NPCUtil startMoveAction / tryStartMoveTalkAction): a Penguin with a rail
    moves on it (Penguin::init's speeds by arg0: Walk 1.5 / Swim 5 / SwimSurface 5 / SwimTurtle 10 / DashA 6, on the
    ground for Walk / Dash, reversed at an open rail's end, the Flow nerve Swim at 5), its action _11C while moving, the
    turn / talk ones standing or in a long talk. Penguins / PenguinCoach / PenguinStudent now talk (talkcls 12: their
    balloon 150 / 430 up). NPCSupportRail: a dead rail holder for PenguinSkater, nothing to run. Test XF="-DGAP_TEST=23
    -DDEBUG_HUD -UCATCHUP" HeavenlyBeachGalaxy/1 (rows 339-347 walking, swimming, dashing, talking). Left: Kinopio's
    rail (tryStartArgs at each point).
  - Beach Bowl's swim test (game.c student_update / coach_takeout / pg_branch / pg_event): PenguinStudent's school on
    its rail (the leader Slow / Lead with its golden shell, KouraShine as an extra JetTurtle Inst.jet 4; the pupils
    _168 behind it, circling it), PenguinCoach arg0 1's branches (carrying, the golden shell), events (SW_A on,
    TakeOutStar: its action, the star from it after, star_pos coach_took). slice.py: EXTRA PenguinStudent KouraShine,
    PenguinCoach arg0 1-3 declares its star. Test XF="-DGAP_TEST=24 -DDEBUG_HUD -UCATCHUP" HeavenlyBeachGalaxy/2 (talk,
    Yes, the school leads, the shell taken, the coach's star demo). Left: the surfing coach modes (arg0 2 / 3).
- mario2 (2026-11-04), #168 PlayerKick: mario_kick / mario_kicked (MarioActor::sendBodyAttack's ACTMES_KICK: his
  cylinder on the sensor, status none, not damaged, no hip drop, within 90 degrees of his front; ランニングキック, its
  voice, pad 2) taken by a staggering Kuribo (36 / 30, three star pieces) / KuriboMini (22 / 25, three), a swooning
  PackunPetit (punchDown) or Petari (KickOut) and a coconut (isValidPushedFromPlayer). Kuribo arg0: no gravity, its
  rotation's up. scatter_pieces_player (MR::scatterStarPiecePlayer); PunchingKinoko's attackSensor as Petari (scatter
  10 at 30 and up, Hit effect, reflection x 1.6 / x 0.3, walkers through walker_blow, CrushedEnd FlipWeakJump,
  stopScene 5). Test `kick` (EggStar 1: a Kuribo spun, Mario walks into it: kicked, 3 pieces); `punch` now waits for
  the mushroom to bind. No pack change.
- mario2, #174 sudden gravity turn: m_gravity_turn (MarioActor::updateGravityVec(false): a turn of 0.4 pi or more
  holds the gravity 15 frames (_334), drops a grounded Mario off (_10._1A: tryJump, ショートジャンプ, the carried move),
  ショートジャンプ in the air, his jump halved / x 0.75, 2D stick cleared (_10.turning), the binder offset blended 15
  frames (_3AA), the head push over 160). Not in a launch star (doRush). Logged "mario: gravity turned N degrees".
  Route EggStar 1: star at f 4135 as main. ponytail: _F74, _2E0.
- mario2, #193 sounds / effects of #143's marks: Rock / Wanwan (rock_roll_se: roll / glitter level SEs, the bark
  every 120 within 600; bound, break, standby SEs; rock_rumble), cannon shells (75 + 100 from a launcher, the shoot SE,
  Shoot / LocusSmoke / Explosion / MisFire effects, KILLER_FLY / EXPLOSION / MISS_FIRE, HipDropDown hop, binder and
  attack x 1.3), MeteorStrike (cannon shoot, FALL once in view, ENV_FLY, MAGMA / BREAK, startRumble), AirBubble /
  AirBubbleGenerator (Wait / Generate after 6, coriolis move, KillWait, the bob, incPlayerOxygen 8, no 6000 limit),
  FireBar (center level SE, FireBarBall within 5000), TreasureSpot / CoinFlower (Glow within 2000, sensor 80 at 30),
  CocoNut (RollingSmoke, Land, WaterColumn, back once its place is out of the view, arg1), Mario's SpinRing / StepOn /
  BarrierBound / WaterBound / AirRecover; Kinopio / KinopioAstro reactions (kinopio_reaction: trample (Mario bounces,
  30 frames apart), hip drop (away jump), spin within 450, pointer; their voices). New effects need tools/jpa.py's
  NAMED / NAMED_ONLY rows in resident.bin; new SE names need every scenario's sound.bin. Seen in PCSX2 (overlay
  out/wt-mario2: resident.bin, EggStar 1 sound.bin): `rock`, `bubble`, `cannon`, `meteor`, `punch`, `npc` with
  XF=-DKINO_TEST (HoneyBeeKingdom 1: trampled x 3, spun); `se?` lines for the new names in the old packs. Left:
  CoinFlower Bloom / AirBubbleGenerator Generate / IronCannonLauncher Shot BCKs (classes not skinned: obj_anim
  would loop their first clip), TicoFat's reaction voices, DinoPackunVs2 (the whole fiery boss: its FireStrong /
  Fire attack variants belong to it), the stomp combo (only KoopaJrShip's 砲弾 and KuriboChief's 全滅用クリボー count:
  neither ported), SE_SM_LV_KINOMES_GOT_MAIL (only with the Wii Message Board mail).
- story, HUD counter animations: tools/layout.py tags the counters' quads (cu / cv / tu / tv of fill 0 / 255:
  wait.brlan's Sha* y curves, flash.brlan's TxtNumber C0, the CountUpPaneRumbler pane), game.c lay_anim_y / lay_text
  play them: Wait from each Appear, Flash + rumbler at a count up (Coin, Star, PlayerLeft's exeWait one by one,
  Purple; StarPieceCounter: Flash up, rumbler on any change, TCounter only). The flash is lyt's lerp(C0, C1, I): the
  glyph through C1 - C0, then C0 added (render.c Quad pad bit 1) under the digit's mask (msg.py: half-size masks
  0xE030 + digit in the text atlas's spare rows). Test: plain EggStarGalaxy/1 XF=-DHUD_TEST (coins, a life, a star:
  `hud test` lines; flash held at its peak from f 1100). Needs resident.bin (+100 B). ponytail: no counter light
  effects.
- story, animated planets: PlanetMap::init's startAllAnim (PlanetMap.cpp:67) for the planets with an own-name BCK
  (slice.py ANIM_PLANETS: PlanetRocket, PlanetDisk, ChoConveyorPlanetB) through AUTO_SKINNED (skinned, obj_anim
  plays the clip; the BTK planets already scrolled through anim_btk). Seen: StarDustGalaxy/1, PlanetRocket's debris
  (PartsA-C) turning, Mario standing on it. ponytail: PhantomHolePlanet (its collision follows the BCK's Move joint),
  BeamGoRoundPlanet / HatchWaterPlanet (own classes), TeresaMansionPlanetB's BRK. Repack StarDustGalaxy,
  PhantomGalaxy, FactoryGalaxy.
  Note for agents: `git stash` is shared by every worktree (one repository): a stash pushed in one worktree can be
  popped by another. Do not use it; commit WIP to the branch instead.
- Release repack (2026-11-06, main 6b9035b + #318): the old packs (all but music / movies) moved to
  out/packs-backup-2026-11-06 (a copy ran at ~1 MB/s on D:). Full repack with main's tools (out/release_2026-11-06.sh,
  log out/release_2026-11-06.log): lyt.py (menus.bin 939 KB, menus3d 174 KB, picturebook.bin 3288 KB new, title 1744 KB,
  ending 1477 KB, subs 143 KB), slice_all.py (resident.bin 4.31 MB with MarioTornado, resident_luigi.bin 4.37 MB; 136 /
  136 converted and fit 9.10 MB; down the shrink ladder: AstroGalaxy 1-5 TridentZone ratio 0.8 / 0.6 / 0.6 / 0.7 / 0.6
  with no layers, OceanPhantomCave 1 0.5 no layers, 2 0.6, 3 0.8, 4 0.7, PhantomGalaxy 1 0.8 no layers), `zone_fit.py
  out/packs` 906 zones, none over (tightest OceanPhantomCave 3 GhostShipCaveZone, 4 KB left; with the disc ELF 4431 KB
  left: ROOM, not the region, is the bound now), every sound pack and the shared sound.bin (1117 SEs, 32 MEs). Every
  line of D:/smg-ps2-wt/repack-todo.md up to #299 is in it. Checks: host tests; route EggStar 1 star at f 4215; coverage
  (out/rel-cover.sh, one galaxy per PCSX2 lock, out/coverage.csv, logs of the failures in out/cover_logs): 118 / 123 ok
  (KoopaJrShipLv1 1 now ok); misses PhantomGalaxy 6, BattleShip 1, CannonFleet 1 (the cover test's landings, as on
  10-30); FactoryGalaxy 2 crashed after its first three-zone swap: TicoFat's goods Anim (and the Creeper leaf,
  HoneyQueen's wing, TicoShop's goods) were allocated in-game above main.c's base_mark and freed by the swap (#314,
  fixed in #318: a pool from game_init; cover Factory 2, EggStar 2, HoneyBeeKingdom 1 then ok); EggStarGalaxy 1 once
  jumped to 0 at boot right after opening menus3d.bin (cover ELF), not again in two more boots (cover, plain): the
  layout-dependent boot crash noted on 10-30 (10-30 tied it to EggStar 1's ME data), still open: it also
  hit the route ELF once with #318 (out/rel-boot-EggStarGalaxy_1-route-crash1.log), the retry got the star at f 4215. Disc: build.sh plain ... FileSelect/1 disc, disc.py ->
  out/smg-ps2.iso (3315 MB, 2472 packs, ELF from 6b9035b + #318, not the boss PRs merged since); the 10-31 ISO kept as
  out/smg-ps2-prev.iso.

## Console release (2026-10-23 / 24)

First runs on Daniel's SCPH-75001 (ORBIT launcher, Neutrino over udpfs from nuld; console log = Neutrino's udptty
broadcast on UDP 18194, captured by a listener on the PC). What the console needed, all in the tree, not committed:
- Boot ELF named in serial form (`tools/disc.py` BOOT = SMGP_000.01): ORBIT's catalog drops a disc whose SYSTEM.CNF
  boots `SLICE.ELF` (its serial must be 11 characters).
- rom0:X* modules (XSIO2MAN, XPADMAN + libpadx, XMCMAN / XMCSERV, `mcInit(MC_TYPE_XMC)`): with the old rom0 ones
  Neutrino's virtual memory card (-mc0) hung mc_init.
- VIF1_ERR.ME0 masked (render_init): the real VIF1 stalls with ER0 when a REF tag arrives mid-UNPACK; PCSX2 does not
  emulate it (the console froze on the first 3D frame).
- `subs.bin` and `music/` kept in the disc TOC (main.c toc_select): the movies had no subtitles on the disc.
Performance (PCSX2 hides it: `vu1Instant = true`; with it false PCSX2 matches the console's VU1, but never its EE,
which runs ~1.5-2x slower than PCSX2 says):
- Triangle strips (`tools/slice.py strip_batches`, VuGroup.strip 1 static unlit / 2 skinned lit / 3 static env) and
  three VU1 programs that run them interleaved (`vu1_tex.vsm` VU1Strip, VU1SkinLit, VU1EnvStrip). Good Egg bench, VU1
  timed: VU1 / GS wait 30.3 -> 7.7 ms. The old indexed programs remain for unlit skinned groups.
- 640 x 448 (render.c HIRES, default; `-DLORES` the old 640 x 224): 16-bit dithered colour and 16-bit Z, FIELD
  mode; same VRAM as before, about the same cost.
- Asynchronous flip (render.c flip_tick, render_flip_wait): the HUD and a FINISH close the VIF1 chain, the VBLANK
  handler shows the frame, the next frame's logic overlaps.
- CATCHUP up to 3 logic steps a frame (real speed down to 20 fps; with 2, Mario moved in slow motion).
- Console, Good Egg: ~21 fps before -> 26-33 in heavy places, 43-58 in light ones. EE bound now (logic ~8 ms a
  step, draw build ~10 ms).
Quality: textures k-means-refined 16-colour palettes and Lanczos downscale (tools/tex.py refine_palette), every
galaxy repacked. Music: out/music copied to out/packs/music, streamed from PACKS.BIN (audio.c DISC ring, main.c
disc_read_idle). Release boot: straight to the title; the scenario menu only with SELECT held. Every scenario has
its own sound pack (`out/sound_all.sh`): the shared one with Luigi's SEs had to drop to 6 / 4 kHz caps.
Tools of the session: disc bench `build.sh bench <blob> EggStarGalaxy/1 disc` (paced summary, `bench fps`), the
fps line every 256 frames. Packs before all this: `out/packs-before-final-2026-10-24`.
Next: EE (draw build: pose / draw_model; logic: launched pieces, items, camera), larger textures where VRAM allows,
triple buffering if the texture cache can spare 560 KB.
Second console round (2026-10-24), from Daniel's report:
- White screen with the HUD after PrologueA: the garden's BloomCube at 255 read the whole 16-bit frame as bright.
  Every 16-bit pixel's alpha bit stayed 1 after the alpha clear; bloom_pass now writes FBA 0 and turns dithering
  off for it (which of the two did it was not isolated), dithering back on at its end.
- Movies on 32-bit frames (render_movie_mode, from movie_play): two 640 x 448 PSMCT32 buffers over the 16-bit
  frames, the Z buffer, the bloom buffers and the cache's first blocks (no scene loaded meanwhile); the 16-bit
  frames come back black after. tools/movies.py encodes at 6 Mbps (8 peak, rd / trellis): 824 MB for the nine.
- Title text broken when "Press A and B" appeared: PSMT8 textures 4-block aligned let the font's 256 x 256 glyph
  sheet and the copyright overwrite each other (PCSX2 hardware and software alike); PSMT8 now page-aligned.
- Layout materials with an adding blend (GX_BL_ONE: TitleLogo's PicBloomA / B, PicFlash, PicLogoShine) are drawn
  added (lyt.py reads the blend mode; the LytMat byte after flags); the title's flash and the logo's glow show.
- Eyes: models whose wait.btp picks the eye texture (FileSelectData*, Kinopio, Penguins, Rosetta, HoneyBee /
  Queen) use its first frame (slice.py btp_first): the file select's Mario head had the shut-eye texture.
- Music on the disc read asynchronously, 32 KB a read into a 1 s ring (audio.c, main.c disc_read_async /
  disc_read_done; cd_read waits for a read in flight first): the old synchronous 14 KB reads stalled frames and a
  half-second ring ran dry while zones streamed. "audio: audsrv ring N bytes" in the log tells audsrv's own buffer.
- audsrv's own ring is 10 KB (80 ms at 32 kHz stereo): audio_feed (the stream only) runs between the catch-up
  steps and after the draw build too; audio_update (with the fades) once a frame.
- Camera pad (CameraLocalUtil / CameraDirector::controlCameraSE): D-pad left / right round the camera by pi / 4 where
  the camera allows it (CameraFollow: its eye's line clear of the map; CameraTower: as many intervals as its yaw
  speed; CameraParallel num1 1), L2 is the Wii's C (reset behind Mario), else the NG sound; D-pad up the first-person
  view (MarioFpView + CameraSubjective: Mario still on the ground, the stick looks, D-pad down / X out). The D-pad no
  longer moves Mario.
- The Gateway's opening (HeavensDoorGalaxy 1, game.c "The Gateway's opening" section, gw_*): RunawayTico, DemoRabbit,
  RunawayRabbitCollect / RunawayRabbit (WalkerStateRunaway), RosettaDemoHeavensDoor1, TicoDemoGetPower on the
  チコガイドデモ sheet; the demo executor starts at a part now (dx_request_at), its kills turn on SW_DEAD
  (NPCActor::kill: Rosetta's 1016 brings the black hole), the talk anim camera follows a cast moved to its PosName,
  two talk anim ctrls no longer re-pause each other; EarthenPipe's SW_B at the exit (the rabbits in pipes).
  slice.py: MODEL_ARC / ANIM_ARC / EXTRA for the rabbits (TrickRabbit / TrickRabbitBaby), TicoBaby, LightDome /
  DomeHalo. Test: `build.sh gateway <blob> HeavensDoorGalaxy/1` (A taps, Mario put where it goes on): the whole
  opening to the spin demo's end. ponytail: the guiding bunny hops along its rail when stuck 120 steps.

- fix/start-crater: the Gateway's pit (HeavensDoorGalaxy 1, HeavensDoorMysteriousPlanet, 650 deep) had no way out: its PullBackCylinder was not converted and MarioRecovery not ported. Now Mario::updateCubeCode / doRecovery, MarioRecovery (start, update, updateJump, calcFirstVector, close) and save / getLastSafetyTrans are in game.c. **Release agent, to repack:** tools/slice.py LOGIC gained PullBackCylinder: every HeavensDoorGalaxy and AstroGalaxy scenario's scene.bin (dev overlay: out/wt-crater). Same branch: NPCs' turn axes (Inst nu / nf, NPCActor _A0 / _B0) were read before game_rebind_ set the instance matrix (zero): Toads drawn tilted, some upside down in the ground (PeachCastleGarden 1 row 60); `npc` with XF=-DNPC_LOOK puts the camera on each Kinopio in turn.
- feat/mario-sidestep: MarioSideStep after Petari (doSideStep, start / close / update, checkWallPush at the end of
  decideWalkAnimation): walking square into a wall Mario leans on it (壁押し), waits (壁ウエイト), walks along it
  (壁右歩き / 壁左歩き at 6), A jumps off, the spin punches off. Test: `build.sh sidestep <blob> KoopaJrShipLv1Galaxy/1`
  (the nearest vertical wall: lean, wait, walk; the airship's cannon shells interrupt it now and then). ponytail:
  checkWallJumpCode (barrier / water walls) not ported.
- feat/mario-slider: MarioSlider after Petari (checkSliderMode / startSlider, start / update / close, calcGroundAccel,
  calcWallHit, postureCtrl, setHipSliderTilt; checkGroundOnSlope's 100 and steep faces on it; no ground spin on it):
  on Slider floors (code 9: ForestHomePlanet in HoneyBeeKingdom, HugeBattleShipPlanet, IceMountainPlanet,
  ScaleDownRelayPlanet) Mario slides sitting (スライダー尻) up to mSliderMaxSpeed 32, steered and braked by the stick,
  hands over to MarioSlip on a slip floor, A jumps with the slide's speed. Test: `build.sh slider <blob>
  HoneyBeeKingdomGalaxy/1`.
- feat/mario-2d: Mario2D / MarioMove2D after Petari (check2DMode, stick2DadjustGround, stick2Dadjust, set2Dmode,
  clear2DStick, calcShadowDir2D) and their hooks in Mario.cpp / MarioMove.cpp / MarioCollision.cpp (the place kept on the
  plane through _688 in writeBackPhyisicalVector, setFrontVec never into the screen, the turn round at once with the
  drawn turn mirrored to the camera, isEnableTurn off, walls facing the plane skipped, createMtxDir's shake absorption).
  slice.py LOGIC gained PlaneModeCube / PlaneCircularModeCube. **Release agent, to repack (scene.bin):**
  TeresaMario2DGalaxy, KoopaBattleVs1/2/3Galaxy, CannonFleetGalaxy, HellProminenceGalaxy, StarDustGalaxy,
  DarkRoomGalaxy, SandClockGalaxy, CosmosGardenGalaxy, BattleShipGalaxy (dev overlay: out/wt-states for TeresaMario2D 1
  and KoopaBattleVs1 1). Test: `XF=-DPACKS_OVERLAY=wt-states build.sh 2d <blob> TeresaMario2DGalaxy/1`. ponytail:
  PipeModeCube (MarioMove25D), the gravity flip's 2D handling, checkForceGrounding not ported.
- feat/mario-jetswim: MarioSwim with a carried JetTurtle after Petari (startJet, jet, dropJet, doJetJump and every
  mJetTimer branch of start / update: the jet speed mSwimFrontJetSpeed (Slow), Z brakes, the T inertias, the jet's
  rotation ratios, the floor's mTurboReductionHitFloor, the stream's mTurboReductionStream, the jet jump out of the
  water, a spin / faint / damage throws the shell along his swim front); a shell taken under water starts the jet
  (receiveMsgTaken); MarioSideStep is off while carrying (MarioActor _468). Fixed: a zone rebind read the held shell's
  row from the new instance set (it could switch to another row); its row / place are kept now. Test: `build.sh jetswim
  <blob> HeavenlyBeachGalaxy/3`. ponytail: no BrakeLamp / SwimBubble / BulletWater effects, the thrown shell flies as on
  land.

- TakoHei / KoteBug (fix/states-takohei, 2026-11-04): ported after TakoHei.cpp / TakoHeiInk.cpp / KoteBug.cpp (game.c
  takohei_update, kotebug_update, update_tako_inks). Test `tako` (HoneyBeeKingdom 1: a TakoHei finds Mario, pursues,
  spits ink that hurts him, a stomp FlatDown, a spin PunchDown; a KoteBug flees, overturns, is stamped; another is spun
  over, then blown). They start once Mario is within 3000 with a ground under them (FlowerRoadZone streams in late).
  ponytail: no star pointer bind, no AnimScale / AnimStamp squash, no PressDown. Repack: slice.py
  TakoHei EXTRA TakoHeiInk (HoneyBeeKingdom, OceanPhantomCave, OceanFloaterLand, ReverseKingdom, KoopaJrShipLv1).
- HoneyBee (fix/states-bees, 2026-11-04): HoneyBee.cpp's own nerves (game.c update_bees): JumpLecture / DropLecture
  (Wait, its lecture clip after 120, talk only in Wait, turning to Mario), Fly on its rail at 5 (FlyWait at points with
  arg0), FlyLectureA / B (its clip, FLY_SOFT, the balloon at its Center joint), every bee's talk (talkcls 10, 150 up)
  and calcFloatOffset. Test: `talk` with XF="-DTALK_CLS=10 -DTALK_ARG0=n" (HoneyBeeKingdom 1). Also: walkers out of
  the view no longer take poses from the 40-slot pool (Honeyhive's 46 walkers held all; its bees were not drawn).
  ponytail: no reaction SEs / Reaction nerve. The FlyLecture bees hover off the ground: the test drops Mario under
  them (seen in logs only).
- HoneyQueen (feat/202-honey-queen, 2026-11-05): HoneyQueen.cpp's nerves (game.c update_queen): the cast of
  ハニークイーン謁見 starts in Demo; her functors on HoneyQueenFirst's parts (フェードアウト closeWipeFade, フェードイン
  openWipeFade + Fade turning to Mario at 5, 謁見 Talk + tryTalkTimeKeepDemoMarioPuppetable), at the demo's last step
  Itch (EventWait, back to her front at 0.2, her talk near), her SW_A: Event (forced talk, then SW_B), After; not a cast
  (scenario 2): Wait / Ready. Talk 950 up, distance 0 (only her MessageArea), SE_SM_LV_HONEYQUEEN_FLY, the wing ModelObj
  on her Center joint (BCK), her ten collisions on their joints (slice.py JOINT_KCL_ONLY: "HoneyQueen#<joint>.<k>").
  Test: `queen` (HoneyBeeKingdom 1, overlay packs): demo, talk, itch, SW_A, event talk, SW_B, Mario dropped on her lands
  and slides off. ponytail: no fur, wing BTK, LodCtrl. Repack: slice_all.py HoneyBeeKingdomGalaxy.
- Small #143 fixes (fix/states-small, 2026-11-05): black hole inhale only for classes with an isMsgInhaleBlackHole
  handler (walker_inhale, their kill); Butterfly's HeavensDoor up from gravity, random first frame, BURST SE;
  QuestionCoin pose type 0 (makeMtxUpFrontPos with LiveActor's default gravity); BeeFlowerHover's nerves (SoftTouch /
  SoftTouchWait / HardTouch / Recover, shake, rumble, arg0 NoInit); TreasureBoxPowerStar stays open, its star at
  OpenWait step 10; AssemblyBlock SEs, Blur, Timer's Disappear; ScrewSwitch / Reverse (bind SEs, MOVE / ON SEs,
  shake, the reverse one throws Mario 28 at step 25, EXPLODE_S, kill; slice.py: both skinned, collision at their
  Screw joint); Poihana WalkAround / GoBack / hip drop and explosion shock. anim.c: a clip name longer than 31
  matches its cut (packs keep 31). Tests: screw (Factory 1, again at f400 once the zone is in), beeflower (Bee Mario
  from f530; his landing on one not seen yet), poihana, plainhole (XF=-DHOLE_TEST). Repack: Factory, BattleShip,
  CosmosGarden, OceanFloaterLand, OceanPhantomCave (ScrewSwitch skinned + Screw-joint collision).
- TicoRail (fix/states-ticorail, 2026-11-05): TicoRail.cpp's nerves (game.c update_ticorails): Wait / LookAround /
  MoveSign (AndTurn) / Move / Stop with isGreaterEqualStepAndRandom, the Spin-timed blends, rail ends clamped, two on
  one rail talk (TalkStart / Talk with Reaction / GoodBye, or TalkCancel), its body pushes Mario. Test `ticorail`
  (Astro 1: nerve log, two pairs talk). ponytail: no ColorChange BRK frame, no LodCtrl.
- Creeper (feat/creeper, 2026-11-05): Creeper.cpp (Ride) in game.c (creeper_*): its CreeperPoints every 50 along the
  rail spring free / bend under Mario, AutoRushBegin (spinning or in the air), HangStart / HangUp / HangDown with
  Mario's GrowPlant* clips, the jump off its head (arg1 / arg2, setPlayerSwingInhibitTimer 60: new swing_inhibit, MarioActor
  _EF6, in isRequestRush), the let-go at its root; CreeperFlower at its head, CreeperLeaf (Wait) at its root, Stalk.bti
  (tools/jpa.py, fx_creeper_tex) on the strips. Test: `creeper` (HoneyBeeKingdom 1, overlay packs): creeper 47 climbed
  and jumped off, creeper 63 slid down. Repack: slice_all.py resident.bin, resident_luigi.bin and HoneyBeeKingdomGalaxy.
- JumpStand (fix/states-jumpstand, 2026-11-05): its Bound / BoundJump / BoundHipDrop / StarPieceBound clips
  (no squash), the throw by endBindAndPlayerJump (m_power_jump, a normal jump status for Low / Middle), steered by
  the stick, Mario's TrampolineJumpLow / Middle / High clips, a star piece takes it. Test `jumpstand` (Factory 1).
- FlipPanel (fix/states-flip, 2026-11-05): the bloom model only on the back side (Reverse: front), none at End; the
  panel just flipped drawn 25 down (calcJointMove); FlipPanelObserver's Complete demo (FlipPanelComplete), its star
  after arg0 + 40 steps, SW_A once no demo runs. Test `flip` (FlipPanelEx 1) from f360, then each panel left on its
  front (one the drop misses is flipped by the test after 3 tries): demo, star, got.
- StinkBugParent (feat/stinkbug-parent, 2026-11-05): StinkBugParent.cpp / StinkBugBase.cpp in game.c (update_sbp): its
  17 nerves (territory, dash, back, head attack, spin / star piece guard, panic / shake under Mario, 子連れカメムシデモ with
  the child's ChildDeath BVA kept, Mario's flight back (Mario::doPointWarp, MarioWarp _45 2: m_point_warp), ダメージ中
  anger + 8 star pieces, HipDropDown and its power star); its collisions SwitchChildJ / Switch (slice.py KCL_PARTS
  StinkBugParent#Switch) on its base matrix. blob.c: a zone blob no longer clears the scene's GPOS (findNamePos was
  empty once a zone loaded). Test: `sbp` (HoneyBeeKingdom 2, overlay packs). ponytail: no BRK colours, no
  overlayWithPreviousScreen. Repack: slice_all.py HoneyBeeKingdomGalaxy.
- PunchBox (fix/states-boxes, 2026-11-05): PunchBox.cpp's doHit / Hit (Break / BreakWater, hidden at step 5, its
  arg0 coins at 15: a wreck of its own model), no hip drop break (_8C never set), no spin break while Mario stands on
  it, its SW_A kill one star piece with its burst. Test `box` (FlipPanelEx 1): the coin now pops 15 frames later
  (logged `wreck row ...`), Mario has moved on (the test no longer takes it). WoodBox / CrystalCageL still open.
- SamboHead (feat/sambo-head, 2026-11-05): SamboHead.cpp as a walker (WALK_SAMBOHEAD, game.c sh_update): its 14 nerves
  (WaitUnderGround / Hide / Appear(Immediately) / Wait / PrePursue / Pursue hops / PursueEnd / HitToPlayer /
  StarPieceHit / StampFall / StampDeath / HitBlow / BuryDeath on SinkDeath floors), the default AnimScaleController's
  hit reaction, SW_A / appear / dead switches, arg0 _B0. Test: `sambo` (SandClock 2: trample; XF=-DSAMBO_ACT=1 spin, 2
  star piece then kick). Only row 179 is out at the start (the others wait on their appear switches). ponytail:
  BindStarPointer (2P pointer only). Repack: sound.py --pack SandClockGalaxy, CosmosGardenGalaxy (SE_EM_SAMBOHEAD_*).
- DashRing (feat/dash-ring, 2026-11-05): DashRing.cpp in game.c (update_drings): its axis (rotation's Y) turning to
  Mario (arg3), the pass (20 along, 200 off) sending passRing to MarioSwim / MarioFoo (sw_pass_ring / foo_pass_ring,
  there unused till now) with arg0-2, 15 active frames following Mario, 180 of revival, its Loop / Start / End /
  Revival clips (slice.py SKINNED_OBJ). Test: `dring` (OceanPhantomCave 3: the ring dash; OceanRing 2 rings are out of
  the water). ponytail: no BRK / BTK / BPK clips. Repack: slice_all.py OceanRingGalaxy, OceanPhantomCaveGalaxy.
- SnowBlockA (feat/snow-block, 2026-11-05): SimpleBreakableObj's mBreakFlags 0x20: a fire ball on it breaks it
  (breakable() kind 7, fireball_breaks from attackFire; SW_A gate), StageEffect SE_OJ_SNOW_MAN_MELT, stopScene 5, weak
  shake, its items, Break effect. Test: `fire` with XF=-DFIRE_AT_SNOW (SnowCapsule 1: row 39 melts, its coin). ponytail:
  its own Break BCK not played (drawn static: gone at once). Repack: sound.py --pack SnowCapsuleGalaxy.
- BombTeresa (feat/bomb-teresa, 2026-11-05): BombTeresa.cpp as a walker (WALK_BOMBTERESA, game.c bt_update): Appear /
  Wait / Wander / Chase / TackleSign / Tackle / Drift / DriftRelease / AttackTongueFailed / Explosion / Disperse /
  ReadyRestart (arg1 -1) with their constants; the tongue joints (bt_tongue: root / end JointControlDelegator
  callbacks on Tongue1 / Tongue2); MarioActor _B94 (m_b94: the tongue on HandR, ハンマー投げ回転中 upper, his spin's
  tryReleaseBombTeresa ハンマー投げリリース, checkSquat refused); its binded KoopaStatue breaks
  (explosion_breaks_inst). Test: `bteresa` (Phantom 1 row 228: chase, tackle, Mario hurt; XF=-DBT_ACT=1: spin, the
  tongue holds his hand 40+ frames, the room's wall cuts it; near the statue its swing breaks it). ponytail: no BRK,
  no BindStarPointer, no ShadowAppear / BallAppear (Polta), no Shock. Repack: sound.py --pack PhantomGalaxy.
- JellyfishElectric / ElectricRailMoving (#260, 2026-11-05): JellyfishElectric.cpp's nerves (Wait / WaitWith
  Right / Left Turn every 280, Damage -> DangerWait, Death: stopScene 5, binder 400, a coin; Attack; RailGoal arg1,
  arg2 back to its start), the sin bob, its rail at arg0, the 350 body sensor (electric, push; spin; JetTurtle /
  explosion / invincible knockOut). ElectricRailMoving.cpp: arg0 lit sections running at arg1 (isValidCoord on the
  rail sensors), its ElectricRailMovingPoints (35, shock even a squat), SW_A Disappear 20 frames. Tests `walker`
  XF=-DJELEC_TEST (OceanPhantomCave 1), XF="-DWALKER_TEST -DERMV_TEST" (BattleShip 2). ponytail: in the header
  comments (point light, star pointer swoon, BRK / BAS colours; the rail drawn as its model over the lit pieces).
  Repack: slice_all.py OceanPhantomCave, FishTunnel, BattleShip, CannonFleet, CubeBubbleEx, Factory, FlipPanelEx.
- SandUpDownTriRock (#274, 2026-11-05): Wait, SW_B on Down (On BCK), Rest arg0, Up (Return BCK), Stop at the
  power star's get; its collisions Move00-02 on their joints (slice.py SKINNED_OBJ + JOINT_KCL_ONLY, game.c the
  "<class>#<joint>.<k>" parts). Test `walker` XF="-DWALKER_TEST -DTRIROCK_TEST" SandClockGalaxy/1 (Mario rides it
  down). Repack: slice_all.py SandClockGalaxy.
- BegomanBabyLauncher (#276, 2026-11-05): BegomanLauncher.cpp mLaunchType 2: arg3 babies (WALK_BEGOBABY slots after
  it, host = the launcher), hidden Wait while one lives, WaitForLaunch 180, Appear 90 (Launcher effect, its clip,
  APPEAR level SE), Launch (launchBegomanBabyLauncher 100 / 10 / 30: BegomanBaby::appearFromLaunch, AfterLaunch Fly,
  AfterLaunchOnGround Bound), Disappear 90; SW_APPEAR (dead until on, WaitAfterSwitchOn arg2), SW_B kills it and its
  babies; a baby's death sets its row's SW_DEAD. Test `walker` XF="-DWALKER_TEST -DBLAUNCH_TEST" CannonFleetGalaxy/3
  (launch, landing, babies killed at 900, relaunch). ponytail: no shadow, its declareCoin. Repack: slice_all.py
  CannonFleetGalaxy, OceanFloaterLandGalaxy.
- BigBubble (feat/281-big-bubble, 2026-11-05): BigBubble.cpp, BigBubbleGenerator / Holder / MoveLimitter (slice.py
  LOGIC Plane / Cylinder), BigBubbleCameraArea (camera.py shape 3: a box while Mario is bound), BigBubbleDrawer with
  OctahedronBezierSurface / BezierSurface (bezier_tab.h: Petari's coefficient tables; jpa.py fx_bigbubble_tex: the
  env map pass and the rainbow added at its mean alpha, one texture a GS pass). game.c bgb_*: the generators (switch
  or distance), Appear / Wait / Capture / Break / Escape / Merged with Petari's constants, Mario bound (SpaceWait, the
  pointer's A pushes, a stroke spins it, Z SwimDive out), merges, limitter breaks, fire floors, electric rails,
  SpaceMines, star pieces deform it, its ride sensor takes coins. Test `bigbubble` (CubeBubbleExLv1 1). ponytail: no
  isPressedMovingWall, ground water, other enemies' attacks, nozzle pointer, shadow, Generate BCK. Repack: jpa.py
  (resident.bin + resident_luigi.bin: BigBubble effects + textures), slice_all.py + sound.py --pack
  CubeBubbleExLv1Galaxy, CubeBubbleExLv2Galaxy, ReverseKingdomGalaxy.
- Unizo (fix/states-unizo, 2026-11-05): Unizo.cpp's nerves (Wait, Jump on its Search BCK, Chase, Attack on its
  Attack BCK, CollidePlayer, CollideEnemy: two Unizos push each other, Shock BCK), mJumpCount, the sea / shoal roll
  bob; doBreak / doFireDown with its break model (UnizoBreak / UnizoLandBreak / UnizoShoalBreak facing away from the
  camera, Break / FireDown / Firedown BCK, killed at its end), Death and shakeCameraNormal. slice.py: the Unizos and
  their break models skinned (SKINNED_OBJ, EXTRA). Test `unizo` (OceanRing 1) from f500: Jump, Chase, hit, Attack,
  CollidePlayer, the test's break at f950, dead ~170 frames later. ponytail: no BRK, no Blink BTP, no damage floor.
  Repack: slice_all.py OceanRingGalaxy and every galaxy with Unizo / UnizoLand / UnizoShoal rows.
- JetTurtle (fix/262-jet, 2026-11-05): JetTurtle::reset (Explosion / ExplosionRed / ExplosionGold by mShellType,
  Star for reason 1, WaterExplosion, SE_OJ_TURTLE_JET_BREAK_W / L), arg2 1 killed, Restart (home at step 45, blinking,
  arg3 1 AppearLight + REVIVE_EFFECT, KouraRevival + REVIVE_APPEAR at 105); the thrown shell ends on its first
  accepted hit or a wall (shakeCameraWeak), sResetStep / 10000 reasons. Gesso knockOut, Poihana tryShock +
  stopScene 3, Snakehead TurtleDown (Damage, back to 300, Death, 9 pieces / a 1-up given at once). Test `jet`
  (OceanRing 1) from f500: taken, thrown, reset 1 at 300 frames, revived 105 later. The walker reactions not seen.
- WoodBox (fix/264-woodbox, 2026-11-05): exeHit step 15 now gives the dead switch (was at the hit), arg3 1
  SE_SY_READ_RIDDLE_S and arg4's 1-up (given at once: no KinokoOneUp actor, ponytail); explosions and fire balls
  break it (receiveMsgEnemyAttack / isMsgFireBallAttack; the walkers' other attacks reach only Mario, ponytail); arg7
  declares its power star (slice.py STAR_DECLARERS: no disc data has one), shown 100 up from the hit, out at kill().
  arg1 respawn was already read (the BVA-broken box stays unless arg1 0). Test `box` + XF -DBOX_WOOD (HeavenlyBeach 1:
  row 356's dead switch 13 at step 15).
- CrystalCageL (fix/269-cagel, 2026-11-05): the first hit sends ACTMES_REFLECT (mario_reflect: reflectWallOnSpinning
  5 frames of mInertiaReflectSlip, walk speed at most 0.5), BVA frame 1 (slice.py EXTRA "#CrystalCageL:1": a
  "<BVA>:<frame>" pose) and the 20-frame wobble (cagel_draw, drawn only: its collision stays put, ponytail); the
  break: CrystalCageLBreak's clip where it stood and the frame-2 stump at _F8 (the map below it). Test `cage`
  CosmosGarden 3 (hit, break `stump -60 below`, star got). Left: 壁スパーク effect.
- SpinDriver / SuperSpinDriver (feat/265-spindriver, 2026-11-05): Capture (arg2, default on: Mario within 240
  without a spin, farther than 350 since the last one, pulled to the centre, turned to the star's pose over 40 / 60,
  let go once centred or shot by a spin), the stars' Y turn (_104 / mFrontAngle with its swing / near / capture
  feeds), drawn with calcInitPose's up / front (arg5), calcShootMotionTime's motion steps for both, Mario's bind pose
  as a quaternion (slerps in Capture / ShootStart, turnBindHead, SpinDriver's calcParabolicBindPose flip).
  SuperSpinDriver binds only after Wait's 90 frames (sCanBindTime). Test `zone` + XF -DCAPTURE_TEST (EggStar 1:
  captured at 150, let go at step 61, again after 1000 away, a spin at step 39: flight 400, motions 75 / 330 / 380;
  -DCAPTURE_SMALL=1 on HeavenlyBeach 1: let go at step 41, flight 80, 2 / 10 / 60). #286: their own clips (every
  BCK is packed): Wait, Appear (its end waits for the clip by length), SpinDriver Active, SuperSpinDriver Start.
  ponytail: no BPK colours, no SpinDriverPathDrawer (no strip draw, no SpinDriverPath textures).
- SmallStone / CircleShell / CircleStrawberry (fix/275-smallstone, 2026-11-05): ring across its gravity
  (makeAxisCrossPlane; arg1: its rotation's -Y), members on the ground by an exact line (map_poly), CircleShell's
  random turn / scale / Kind BVA frame shuffled with its Break1-3 effect, the centre as the members' box and the Range
  sensor from it; the spin breaks them on the next frame (own Break effects, a star piece each always: arg2 only sets
  the water check), the next spin kills it; tryShake WindLoop / WindEnd at its rate, tryPush 20 x scale on Mario.
  Test `stone` (CubeBubbleExLv1 1: two stones broken, 8 effects each, shaken at 2.57; OceanRing 1 repacked:
  CircleShell 8 Break1-3 effects). Repack: CircleShell / CircleStrawberry skinned (resident + their galaxies).
- GhostPlayer (feat/270-ghost-player, 2026-11-05): Cosmic Mario's races (HoneyBeeKingdom / IceVolcano / OceanRing /
  ReverseKingdom 4) after GhostPlayer.cpp / GhostPacket.cpp. slice.py GHST: the stage's .gst files as on disc plus
  the marioAnimeTable / group names by their Shift-JIS getHashCode (ghost_names; the packets name clips by hash or by
  Shift-JIS string); GhostMario / GhostLuigi skinned, posed by Mario's clips (same 30 joints), their star-field
  texgens at the textures' mean colours (ENV_MEAN, ponytail). game.c update_ghost: the appear switch, PreStartDemo0-2
  (レース開始1-3 cameras: camera.py's actor camera names now split the %03d set off an event ending in a digit, which
  also fixes MorphItemObjNeo's 出現1 / 出現2), the レース準備 demo with Mario as puppet, RaceManagerLayout (menu.c
  menu_race_*: Race in menus.bin), the packet stream, push / trample star pieces, WinDemo (BGM_RACE_LOSE, Lose,
  forceKillPlayerByGhostRace: miss() as EventGhostRaceDown, レース負け, no MissLayout, wipe 92-200), LostDemo, the
  shell (JetTurtleShadow's Koura at HandR). Mario: the start dash (_436 noticeDashChance, _434 item dash ratio).
  sequence.c seq_is_retry (hasRetryGalaxySequence: A skips the start demo after a miss). Test `ghost` (+ XF
  -DGHOST_WIN: Mario put on the star): HoneyBee 4 the ghost wins at f ~4300, OceanRing 4 (swimming, the shell) too.
  Left: the upper-body XanimePlayer (only PartsControl's hand pattern), JetTurtleShadow's draw flag, the dash's blur.
- ChipCounter (feat/290-chip-counter, 2026-11-05): menu.c chc_* (BlueChipCounter / YellowChipCounter / CollectCounter
  in lyt.py COMMON: menus.bin +84 KB): Hide / FrameIn / Show / FrameOut / TryDemo / Complete / CompleteOut, Chip1-5
  ChipGet, ShowHide by short balloons, the count over Mario; game.c chip_group_*: UI range (chips' box +- 2000 or
  arg2), the appear switch's show (arg1 flash frames), noticeGet (show with the old count, setCount, the last one asks
  the チップコンプリート demo: pd), SW_A at the demo's end. Test plain + XF -DCHIP_TEST (EggStar 1: five chips, counts
  1..5, Complete, `YellowChipGroup 0 complete`). ponytail: arg3's チップ集め済み flag, the chips' flashing (arg1: no disc group sets it).
- MarioFlow / FountainBig (feat/294-flow, 2026-11-05): DMS_FLOW in the status machine (doFlow, refused while
  flowing or in 空中ひねり; start 中ダメージ, mJumpHeightFlow 18 up + mJumpDistFlow 2 ahead, mGravityFlow 0.3 a frame,
  中ダメージ空中 at 20, a spin after 30 spin-jumps, the landing 中ダメージ着地 / 共通ダメージ着地 / 吹っ飛び倒れ, slides
  x 0.95, A after 15). FountainBig's nerves at Petari's steps (Wait 120, Sign 80, SignStop 30, Spout 180, SpoutEnd 10),
  its SEs, its eye sensor (60) on the axis 60..600 at Mario's foot, a weak rumble, a push outside 60 across gravity,
  Bee Mario flipped (FlipWeak on the ground) with mSpoutTimer. Test `fountain` (HoneyBeeKingdom 1: MarioFlow, 303 up
  at step 20 as 18 x 20 - 0.3 x 190).
- beeflower / cocoon tests (#296, 2026-11-05): beeflower's Bee Mario stopped at the first-time 初出表示 window (A now
  pressed at f640: Bee Mario lands on flowers 21 / 22); cocoon's hooks ran inside CocoonEx's arrival flight (moved to
  f520-700: hangs on, aims, drags, launched 419 off, the flight ends at 90). Game code unchanged.
## Rules

- Fidelity first (user requirement, 2026-10-11): behaviour is ported exactly as Petari (vendor/Petari) has it -
  constants, reference frames, joints and hit sensors, nerves and their order, timings, clip names. No values
  guessed from data and no approximate versions where the decompiled code exists; code written before that way is
  rewritten (Good Egg first). Only where Petari lacks the code (undecompiled functions, commented-out constants,
  services the PS2 port does not have) is a gap filled, marked `ponytail:` naming it. 30 fps is acceptable.

Petari (vendor/Petari, not tracked here) updated 2026-10-14 to 601f78c (2026-10-13): decomp.dev reports it 100 %
decompiled (68.75 % linked). Commented constants without values (SurfRay, Begoman...) are still unknown. Our disc is
RMGE01 (US): where Petari has `#if VERSION` branches, port the RMGE01 one. To check against it in PCSX2: the talk
text's picture offset (`y - 2` in the tag loop; US: -ascent, no mPictureFontOffset) and the character spacing
after tags (US: mNoCharSpace starts true, font size tags return NO_CHAR_SPACE when measuring).

## Done

- Milestones 1-4 GO (resident budget, textures, Mario, cameras).
- Milestone 5, task 5.1 done: every galaxy/scenario converts into streaming packs (`tools/slice_all.py`).
  With every class that has a model converted and indexed geometry: **136 / 136 scenarios fit 9.01 MB** with
  streaming (worst OceanPhantomCave 1, 6.98 MB).
- Task 5.2 in progress. The engine plays any galaxy from its packs:
  - world space; gravities point, plane, plane-in-box / cylinder, cube, disk, segment;
  - 12 camera types;
  - zone streaming keeps several zone chains full at once (every zone within 1000 of Mario, then those ahead,
    nearest first, while the stage packs fit 9.01 MB minus the base packs: `target_set` in main.c, sizes read at load);
    a zone is read at once only when Mario is off every full zone's model (or out of a pipe), else in the background;
  - streaming room: 9.01 MB minus scene.bin + common.bin (resident.bin has its own budget), and the nearest needed
    zone always comes in (OceanPhantomCave 1's start zone did not fit: Mario fell forever);
  - collision: an archive's own `<name>.kcl` first, not a `movelimit.kcl` listed before it (PuffballPlanetA had no ground);
  - zone streaming with prefetch and per-zone spheres (a pipe's exit zone is wanted at once), from `host:` or from
    the **disc image** (`tools/disc.py`: one sector-aligned PACKS.BIN + PACKS.TOC, read with libcdvd, prefetch
    asynchronous: no slow frame while streaming; ~1.5-2 MB/s in PCSX2, a zone wanted at once blocks ~1 s);
  - texture cache in GS memory, frustum culling, indexed geometry;
  - launch stars: SuperSpinDriver (+ Pink / Green), SpinDriver (rail flight or a throw along its Y axis);
    in flight Mario's head points along the path (turned 0.4 a frame), he spins and flips before the end
    (updateBindActorMatrix), the clips follow calcShootMotionTime, he is let go with the path's last velocity, and a
    chase camera (1300 behind, 400 above, eased) stands in for SpinDriverCamera's event camera;
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
    camera); star pieces shown within 24 px of it (and 4000 of the camera) are taken; drawn as StarPointerLayout
    (menu.c `sp_*`: DPDPointer in menus.bin, its nerves, the blur, the OnOff controller's modes: the star in play,
    the finger in menus, the open / gripping hand for pull stars and cocoons, hidden in demos);
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
  - Teresa (Boo: shy when Mario faces it, chases his back, hurts, a spin drifts it into hiding), BeeFlowerHover (turns;
    Mario, never a bee here, goes through it, back 120 frames later), Butterfly (runs from the pointer, a star piece
    when pointed at), TicoRail (Lumas strolling their rails), JumpBranch (grab in the air, swing, X jumps off, a spin
    lets go; hangwait / jumpspin stand in), DeadLeaves / Pompon2Plant / Pompon4Plant (a spin / a touch / the pointer
    brings out their item once), BigBirikyu / Birikyu (electric balls on rails or circling, R2 holds them),
    PunchingKinoko (a spin punches its head away, it swings back, blows walkers away, knocks Mario, a hip drop crushes
    it; the posed model is tilted and stretched to the head), CocoNut (spin / hip drop / kick / star piece send it
    rolling: bounces, wall reflection, turning; after a spin it blows walkers away and breaks, back at its place);
    SpaceCocoon (Mario in the air hangs on; R2 with the pointer on it aims, the cocoon follows the pointer on its
    plane; let go 100+ off and he flies off towards its neutral point at 60 for arg0 frames; X lets go; no stalk drawn);
    Fluff (a spin sends it up, it drifts down; Mario in the air takes hold, the stick steers, Square spins up 3 times,
    X jumps off; FluffWind: #328); a rebind keeps what Mario rides (by its row);
  - StinkBugSmall (a walker: in its territory it turns to Mario, rushes at 20 up to arg1, walks back; its horn hurts,
    a spin or star piece makes it guard, a hip drop kills it: 3 star pieces; no Panic / Shake on its back);
  - Unizo / UnizoLand / UnizoShoal (walkers: jump and chase Mario within 1500 zigzagging, roll, hurt on touch and
    bounce off, rest 120; stomps and spins do nothing; enemy attacks break them);
  - NoteFairy (on its appear switch its notes come out along its rail as it flies, taken on touch; all taken: SW_A
    on and its power star (arg7, slice.py declares it) out; arg2 seconds to take them; no melody / demo);
  - KeySwitch (an item: taken, its SW_A on; no pop-up on its appear switch); Poihana (a walker: chases Mario
    within 800, catches him in front and throws him up at sqrt(2 x arg0), a spin knocks it out 150 frames; no
    sleep / drowning); walkers wait while their zone has no collision bound (zone_kcl: they fell through low zones);
  - JetTurtle on land (Mario takes it on touch, a spin throws it at 30 / 20 along his front, it blows walkers away,
    Unizos too, gone on a wall or after 300 / 480 frames, back home; no water jet, no homing);
  - ScrewSwitch / ScrewSwitchReverse (a spin on it: Mario held while it screws, 34 / 75 frames, then its SW_A; the
    screw itself does not move);
  - PowerStarAppearPoint (kept as a logic row): an event star of a group with appear points comes out at the one
    nearest its declarer (ReverseKingdom 2: the NoteFairy's star);
  - PhantomCandlestand / TeresaMansionCandlestand (burning, their Fire effects kept; the flame burns Mario, a spin
    puts it out: its coin or star piece, it stays out); CandlestandIceVolcano (unlit with a SW_A until a fire ball
    lights it, then its SW_A; a spin makes it flicker; `XF=-DFIRE_AT_CANDLE` fire test);
  - FountainBig (a water column on its 420-frame cycle, grown and shrunk; Mario in it thrown 44 up, once a landing;
    its sign / spout effects within 3000 of the camera: all of HoneyBee's at once made 81 slow frames);
  - SimpleBreakableObj kinds in breakable(): IcicleRock, SandUpDownTowerBreakableWallA / B (a spin),
    TeresaMansionHoleCover (a hip drop); arg3 -1 coins / 3 star pieces by arg6 (`XF=-DBOX_ONLY_NEW` box test);
  - Fire Mario (MorphItemNeoFire: 1200 frames; each spin throws one of 3 fire balls along front + up at 30, gravity
    2, bounding 0.9; they burn walkers, melt SnowMen: head / body then down, SW_A / SW_B / SW_DEAD (SnowMan not
    tested live); Mario keeps his colours, no fire animations);
  - Bee Mario (MorphItemNeoBee, until hurt: A held 30 frames into a jump lifts him up to 5 a frame for 180 frames of
    flight, refilled on landing; let go he drops at most 5; BeeFlowerHover holds him; no wall walk / honey, same
    colours);
  - CollectTico / StrayTico (touched, a stray Luma follows Mario; all of a CollectTico's zone rescued: its power star,
    slice.py declares it; no bubble, no ring demo);
  - swimming (WaterCube / WaterSphere / WaterCylinder kept as logic rows; Mario's middle in one: X strokes 4 up to
    11.5 along his swim direction, the stick steers, R1 / L1 tilt down / up, 0.985 decay, floats up to the surface and
    stays, X there jumps out; oxygen 3600 under it, then a life piece a second; no spin dash, swim animations,
    OceanRing water; OceanSphere (100 x scale) / OceanBowl (under its plane) count as water);
  - AirBubble (touched or spun: all the oxygen back) / AirBubbleGenerator (a rising bubble every arg0 = 180 frames,
    life arg1 = 360);
  - DinoPackun (Vs1; its SW_A from the planet's SwitchSphere starts it): egg / Lv1 / Lv2 / Lv3, walk, turn, find, chase,
    cool down; a spin at its tail ball sends it out and back into its head; the fourth hit brings its star (slice.py
    declares it); no tail chain (the ball 600 behind), animations, demos or star pieces;
  - TicoFat after Petari: Prep until its talk would end (no messages: at once, in front within 280 x scale), Wait /
    Point by updateGuidance / updatePointing, L1 (B) held with the pointer on it gifts a piece every 5 frames to its
    Mouth, Eat (EatStart / Eat) and growth (1 + 0.9 x eaten / arg1), Chem, Test, Fullness (Joy), Demo (hidden at 90),
    Fly along its rail (SpinDriverShootPath, eased, 100 a frame), WipeOut / WipeIn (SW_A), Info, After: its group
    appears; clips through its ActorAnimCtrl actions (slice.py packs aliases); Normal BRK frame 0; no meter, multi-
    actor cameras, wipes, information message, FatRed by size; its pink needs lighting the bake lacks (beige);
  - OtaKing (Lv1) after Petari: WaitOnSwitch, AppearDemo (Mario hidden), the Wait / ThrowCocoNut / ThrowFireBall
    loop with isValidThrowCocoNut / FireBall, coconut rallies (CocoNutBall: aimed at Mario with the _C0 / _C4 height
    rule, sent home along a 42 a frame arc with gravity 2.2 and its sideways curve, back at 15 + 3 x rally turned
    12.5-17.5 degrees, a touch only flips Mario), HitBackStart (face) / HitBack (body), Damage / PowerUp / Down,
    DownDemo (300) and AppearStar, NearAttack, BubbleAttack with six FireBubbles, FireBalls (three, 0 / -30 / +30);
    turning at 0.003 up to 1 a frame; no magma, long feet, star pointer, star pieces;
  - skinned placed objects (slice.py SKINNED_OBJ: DinoPackun, OtaKing, OtaKingFoot, TicoFat, Tico, CocoSambo,
    CocoSamboHead): drawn with their skeleton, their BCKs in the scenario's ANIM chunk ("<class>/<clip>"); a pose
    pool by row (obj_anim, the class's wait clip; one-shot clips go back to it); anim_pose / anim_skin split for joint
    overrides; DinoPackun plays its state clips and demos (opening / cry / angry / down with their lengths), its tail
    a FABRIK chain between the clip's TailRoot and the ball, shell and ball on their joints; OtaKing and its feet
    (L / R) play appear / wait / attack / hitback / damage / crowndamage / down; TicoFat eats (eat0-2) and changes
    (metamorphosis, 80 frames) before its planet;
  - demo cameras: slice.py bakes each skinned class's .canm (CANM linear / CKAN Hermite, tangents / 30) per frame
    into CANM; demo_camera plays one in the actor's frame (camera_set_override), Mario waits meanwhile; DinoPackun's
    demos reset it to its spot and set Mario at (0, -550, 800) (resetPosition); it only moves in the battle and never
    falls where there is no ground (its nest: it sank to the planet's centre and the camera with it);
  - PowerStar get demo (PowerStar::exeStageClearDemo): the frame _B8 (up against gravity, its placed Z) at the star,
    Mario set there (setPlayerBaseMtx) playing powerstarget, the PowerStarGet .canm camera in it (519 frames);
  - ScenarioStarter (kept as a logic row): Mario flies its rail in at the scenario's start, after Petari: no
    ShootStart, SpaceFlyStart / SpaceFlyLoop (a whole number of 15 within min(0.2 x flight, 90)) / SpaceFlyEnd / Fall,
    the first pose of SpinDriverShootPath::calcInitPose (arg5 turn);
  - SpinDriverCamera / MultiEventCamera for every launch flight (ScenarioStarter, SuperSpinDriver*): camera.py keeps
    the bcam's event cameras "e:<class>:<CameraSetId>:NN" (class hash, set << 4 | NN, evfrm), slice.py the rows'
    CameraSetId (CSET); the engine runs them one after another over their evfrm with their camint, their target a
    matrix turned to the flight's direction (updateTargetMatrix), until he lands (endForceAtLanding); new camera type
    CAM_TYPE_OBJ_PARALLEL (CameraObjParallel); the old made-up flight follow camera is gone. CAMS entries are 132
    bytes now: every galaxy needs repacking;
  - walkers animated too (Kuribo, Takobo, Karikari, Butterfly, PackunPetit, KuriboMini: walker_clip by kind and
    state, Kuribo facing its way); skinned shapes without texture take the TEV constant colour; BRK registers at a
    frame (tex.brk, slice.py BRK_POSE: Tico's ColorChange 0, its yellow; a saturated bake then keeps the first
    stage);
  - BVA visibility for static models (slice.py VIS: the egg shell's intact / cracked pieces, normal.bva's last frame;
    EXTRA "<archive>#<BVA>" bakes another, DinoPackunEggShell#EggBroken: the cracks in exeEggBroken's first 30);
    DinoPackunEggShellBreak skinned on the EggShell joint, its Break clip from step 30 to 120 (BRK Break baked);
  - BrightSun as Petari (BrightSun::control / controlSunModel): the Sun model at the camera + its rotation's z x
    100000, scale 100, turned to the camera, drawn with the skies (it sat at its placement: its glow next to the
    camera, half dropped by the guard-band clip, tinted half the screen); no lens flare;
  - CocoSambo after Petari (body CocoSamboBody + CocoSamboHead): HideWait / Appear within arg0 (1800) / Wait /
    Attack within 700 / AttackInterval / Hide; spine and head sensors at their joints; a coconut on it makes its head
    fall (45 frames, twice the gravity), Land, Swoon 180, SwoonEnd while the body hides and recovers; a stomp presses
    (35), a spin blows (20) the swooning head: its dead switch; no star pointer, scale reactions, needle material;
  - Red / Black comet time limits (CometEventKeeper::sTimeLimitInfoTable, main.c by scenario): TIME m:ss on the HUD,
    at 0 a miss (the time starts over), a power star stops it;
  - slice.py MODEL_ARC: OtaKing, KoopaJr and Tico take their own archive (NameObjFactory's first was FireBall /
    KoopaJrLow / TicoBaby); a Purple / Black comet scenario's star is the 100-coin one (getAppearPowerStarObjName);
  - SW_SLEEP shows / hides rows like SW_APPEAR (SleepControlFunc); cylinder areas are 500 x scale_y high
    (AreaFormCylinder, was 1000); 100 purple coins bring the 100-coin star too (ScenePlayingResult::incPurpleCoin);
  - WaterPlant (kept as a logic row): arg0 plants within arg1 dropped onto the ground, camera-facing strips of three
    segments swaying (render_strips), WaterPlantA-D packed after the effects' textures (PTCL header word 2);
  - ambient effect objects (NameObjFactory's *EffectObj*: SnowS, LavaSparksS, AstroTorchLight...; tools/jpa.py AMBIENT,
    `ambient_effects`): the 12 nearest within 5000 of the camera play their AutoEffectList group, break ones once on
    their switch. (The hang they caused: the 12-nearest insertion read an unset distance and kept a garbage row, whose
    fx -1 read defs[-1]: thousands of emitters with junk resources and junk texture indices, the VIF chain never ended.
    fx_keep / fx_emit now refuse fx < 0.) `XF="-D..." build.sh ...` passes extra defines for such bisects;
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
- Good Egg fidelity rewrite, part 1 (2026-10-12; improvised code redone after Petari, first tested together):
  - walker kit (`w_*`: Spine nerve stepping, Binder sphere, turnVecToVecCosOnPlane, sight fan, isFallNextMove by
    stepped spheres, WalkerState Wander / FindPlayer / Chase / Stagger, Mario's offence by his sensor cylinder
    `mario_offence` / `mario_touch`, MR::startAction with the clip's own loop mode: `anim_start`, `act_rate`);
    Kuribo, KuriboMini, Takobo (SpinHitController), Karikari (KarikariDirector, clinging at Mario's joints),
    PackunPetit (its blown head), Butterfly ported literally; star piece shots, coconuts, rocks, killer explosions
    reach them through `walker_blow`; `scene_stop` (MR::stopScene);
  - launched star pieces (`launch_pieces` / `drop_pieces`: StarPiece::launch / exeFall; 256 extra slots reused);
  - Rock / RockCreator / WanwanRolling (nerves, rail point arg0..2: PATH records now carry arg1 / arg2), break models;
  - DinoPackunTail physics (nodes, keep distance / bend / to the clip, locks), 8 star pieces per battle, stop scenes;
  - OtaKingMagma (its clips at the ripple steps) and the four OtaKingLongFoot; hits x 8 star pieces;
  - HomingKiller roll / up / chase height / own effects / explosion on walkers; Birikyu group stop and face;
    CocoNut water and effects; NeedlePlant Shake and break; PunchingKinoko's Ball joint; BlackHole SW_A and inhaling;
    ElectricRail SW_A; CollapsePlane Vanish; WoodBox / CrystalCage break models and leftovers (`wrecks`, VIS frames);
    BenefitItem turn / appear-ground / escape; EarthenPipe bound sequence (`pipe_bind`); FlipPanel clips;
  - tryStartAllAnim: placed classes whose archive holds `<archive>.bck` are drawn skinned playing it (`AUTO_SKINNED`);
  - effects added to `tools/jpa.py` FX (fx_tab.h regenerated): every pack needs a repack.
- Good Egg fidelity rewrite, part 2 (2026-10-12): Mario's movement after Petari (`m_*` in game.c): every constant from
  gMarioConstDefault (`tools/mario_const.py vendor/Petari engine/src/mario_const.h`, struct MC with Petari's names);
  inputStick (x 1.5, angle snap), mainMove (standing turn over 10 degrees, turn speeds by walk speed, weak turn,
  U-turn arc, turn ready / ターンブレーキ, stick margins, wall push), decideWalkSpeed / updateWalkSpeed / decideInertia
  (sSpeedTable / sWalkTargetTable, slow start, start spin), decideWalkAnimation (基本 blend: heaviest of WalkSoft /
  Walk / Run / Wait, its speed from the foot steps, RunEnd brake, DamageWait), squat walk; tryJump (1st / 2nd / 3rd,
  turn, long, back), tryDrop (mDropWaitTime grace), procJump (mGravityJumping x mGravityRatioA 0.5 while A is held or
  falling: a single jump now reaches ~270, airborne ~49 frames), doAirWalk, the spin jump (mSpinJumpGravity, 24
  frames) and 空パンチ, MarioMagic's ground spin, jumpToHipDrop / procHipDrop, MarioWall (stick, slide, kick with the
  triangle-jump mirror), doLanding (run on, hard landing, landing clips); interpolation frames from marioAnimeTable.
  Tested: EggStar 1-4 `route` (all four stars), `hip` (Factory 1), `walker`, `bench` on EggStar packs.
  ponytail gaps: our collision for the Binder (no ceiling, height by probe), no 2D / skate / ice / sand / slider /
  side step / slip up / cliff hold, the standing turn snaps (no drawn ease), one clip of each blend.
  Launch stars: SpinDriver's ShootStart dip (200 under it along its up, (2 (p - 0.5))^4 over 20 frames); every release
  is endBindAndSpinDriverJump = tryForcePowerJump (kind 13, gravity mGravityJumping[9] x 1, no air walk, no drop cap
  if kicked down, walls leave the vector). SuperSpinDriver's operate ring never moves Mario (_17D is never set).

## How to run

| What | Command (from Windows, `MSYS_NO_PATHCONV=1` in Git Bash) |
|---|---|
| Convert one scenario (embedded blob) | `python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin` |
| Convert everything (packs + table, ~5 min) | `python tools/slice_all.py data/fs/files out/packs 6` then `python tools/vram_need.py out/packs` |
| Build, embedded Gateway 2 | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench]` → `engine/slice[-bench].elf` |
| Build from packs | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh MODE out/slice/hd2.bin Galaxy/n` → `out/slice-packs[-MODE].elf` (run from `out/`: `host:packs/`) |
| Modes | `plain`, `bench`, `slope`, `zone` (first SuperSpinDriver at frame 120), `box` (FlipPanelEx 1: the coin pops and is taken), `collapse`, `hazard` (EggStar 1: third NeedlePlant prick is a miss; BattleShip 2: mine, electric rail, black hole; Factory 1: FireBar), `item` (BattleShip 2; CosmosGarden 1: a QuestionCoin's dead switch brings star pieces), `spin` (BattleShip 2, CannonFleet 1), `spingate` (Square every 200 frames, Mario's spin logged: PeachCastleGarden 1 none; with XF=-DSPINGATE_TEST the `gateway` test goes to Rosetta's spin demo at once: none until InformationObserverSpin's A, then spins; a direct boot elsewhere takes スピン権利 as passed), `pipe` (BattleShip 2), `hip` (Factory 1), `flip` (FlipPanelEx 1: 18 panels, the group completes, its power star comes out and is taken), `cage` (CosmosGarden 3, SandClock 1: two spins at the CrystalCageL, then its star), `restart` (BattleShip 2: a miss after each of two RestartCubes), `spot` (BattleShip 1: cages whose dead switch bursts a StarPieceSpot), `cannon` (CocoonEx 1: Mario in a launcher's line of fire), `shoot` (StarDust 1: ShootingStar bursts), `plant` (EggStar 1: Mario through every member of the plant groups with items), `steam` (IceVolcano 2: Mario in a LavaSteam vent), `geyser` (IceVolcano 2: Mario in a LavaGeyser column), `assembly` (StarDust 1: Mario over 20 AssemblyBlock places), `jumpstand` (Factory 1: dropped on a JumpStand, then in a hip drop), `tornado` (SandClock 1: Mario put in a TwisterSan, a spin 60 frames after it holds him: TornadoMario Start, Loop, End logged; needs resident.bin with MarioTornado), a plain build with XF=-DHOLE_TEST (BattleShip 2: Mario put in the first black hole once free: its inhale demo, the miss at step 60), `warppod` (AstroGalaxy 2: into a visible WarpPod twice, made active: ポッドワープ開始, hidden in flight, ポッドワープ終了; HeavensDoor 2: an invisible one), `tsuki` (IceVolcano 1: Mario on a Tsukidashikun), `kiraira` (OceanPhantomCave 1: Mario into a Kiraira every second), `takobo` (EggStar 1: a Takobo punches, a stomp, a spin), `meteor` (FloaterOtaKing 1: Mario under a MeteorStrike; the test shows them without their boss switch; `TEST_SHOWN` in game.c), `killer` (BattleShip 1: Mario 1500 in front of a HomingKiller), `mkoopa` (Factory 1: a MechanicKoopaMini fires, a stomp, a hip drop), `karikari` (EggStar 1: Karikari cling, bite, a hip drop throws them off), `flow` (CocoonEx 1: Mario takes moving StarPieceFlow pieces), `cursor` (CocoonEx 1: the pointer sweeps the screen taking star pieces), `gcap` (StarDust 1: R2 on a GCaptureTarget pulls Mario, then he floats and falls), `shoot2` (EggStar 1: a star piece shot stuns a Takobo, a stomp), `tspot` (BattleShip 2: shots open TreasureSpots), `kmini` (BattleShip 1: KuriboMini stomped, spun), `kick` (EggStar 1: a staggering Kuribo kicked), `npc` with XF=-DKINO_TEST (HoneyBeeKingdom 1: a Kinopio trampled, spun), `karon` (SandClock 1: a Karon broken and back), `prom` (HellProminence 1: Mario on a LavaProminence's rail), `stone` (CubeBubbleExLv1 1: spins break SmallStone rings), `teresa` (Phantom 1), `beeflower` (HoneyBeeKingdom 1), `butterfly` (HeavensDoor 2), `ticorail` (Astro 1), `jbranch` (OceanPhantomCave 1), `giver` (ReverseKingdom 1 / HoneyBeeKingdom 1), `biri` (BattleShip 2), `punch` (EggStar 1), `coco` (OceanPhantomCave 1), `cocoon` (CocoonEx 1), `fluff` (CosmosGarden 1), `waterplant` (OceanRing 1), `stink` (KoopaJrShipLv1 1), `unizo` (OceanRing 1), `note` (EggStar 1), `key` (Phantom 1), `poihana` (HeavenlyBeach 1), `jet` (OceanRing 1), `screw` (Factory 1), `candle` (Phantom 1), `fountain` (HoneyBeeKingdom 1), `fire` (OceanPhantomCave 1), `bee` (HoneyBeeKingdom 1), `stray` (Factory 2), `swim` (HeavenlyBeach 1 / FishTunnel 1: 300 under a water surface, floats up and stays), `bubble` (HeavenlyBeach 1: Mario over an AirBubbleGenerator), `dino` (EggStar 1: the tail ball punched four times, the star taken), `fat` (EggStar 2: Mario before the TicoFat pointing with L1 held: 100 gifts, its fly, planet D and the pink SuperSpinDriver), `ota` (EggStar 3: Mario on its bath's rim stepping into returned coconuts, rallies of 1 / 2 / 3, the star taken), `purple` (EggStar 5: 100 purple coins, then the 100-coin star), `walker` (EggStar 1: Mario by a Kuribo, Takobo, Karikari, Butterfly), `kgun` (EggStar 3: Mario held at a KillerGunnerSingle's shell: hit every 278 frames), `ibubble` (EggStar 3: Mario off, then into the ItemBubbles: they burst), `orock` (EggStar 3: Mario 700 before each OtaRock spinning its coconuts back: down, its TakoBarrelA breaks), `rock` (EggStar 1: every RockCreator out from the start, Mario held 3000 over the first till 1800, then let go; prints the farthest a rolling rock gets from its rail, `rock test end: ... ok` under 1500; EggStar 2 rock 349 spawns 140 over its egg planet, deeper than the KCL thickness, and sinks into it, HoneyBeeKingdom 1 rails run high over flat floors: both over 1500, unrelated), `hhp` (EggStar 1: Mario 650 before the HammerHeadPackun is hit (Hit, Crow), a spin in its Rest (Damage), a trample: down; its Plant grows), `slip` (EggStar 1: Mario left still on a Slip floor of DiskGarden slides down it), `pullback` (HeavensDoor 1: after the opening's talks Mario dropped into the planet's pit over its PullBackCylinder: MarioRecovery carries him out, `recovery: done`; needs the repacked scene.bin), `crater` (HeavensDoor 1: Mario dropped over the planet's invisible WarpPod: 惑星貫通 straight through its tunnel, power-jumped out of the other mouth onto the surface), `talk` (EggStar 1: before the first SignBoard, then the first Tico: A opens their talk, A turns its pages), `invinc` (EggStar 3: the rainbow star taken, a walker blown at a touch, a hit for nothing, 1200 frames), `replica` (EggStar 2: Mario on a CoinReplica takes it only once its SW_A is on), `follow` (EggStar 2: a StarPieceFollowGroup's SW_A on: its pieces ring Mario, he takes some, it ends at 400 + 120), `seesaw` (EggStar 3: Mario dropped on a PhantomShipHandle's side turns it; the hip drop half misses its spokes, unverified), `petari` (EggStar 1: a Petari comes out and surfaces, a spin swoons it, a trample smashes it: star pieces), `vine` (EggStar 1: the Plant's appear switch on, it grows, Mario jumps into it, spins up it and jumps off its top), `route` (EggStar 1-4 played through from the start: launch stars toward the goal; a hidden one's switch writer (chip group, cage, CocoSambo) done first, walked to within its zone; then the boss / TicoFat and the star: all four get their star), `story` (AstroGalaxy/1: a fresh file moved to AstroDome/1 plays ドームレクチャー１, X held every second turns the Butler's pages), `report` (EggStarGalaxy/1: a faked EggStar star 2: the star return demo, the result, バトラー報告), `spiece` (EggStarGalaxy/1: a faked EggStar star 1 with the dome lecture heard: the star return, the result, the save, the Butler's star piece lecture with five lock-on shots), `newfile` (FileSelect/1: the card's files; XF=-DNEWFILE_SEED: file 1 seeded with a grand star, still loaded as after a quit, saved; then file 2 new, its icon, Start: `gamedata: file 2 loaded: 0 stars`, PeachCastleGardenGalaxy/1; XF=-DNEWFILE_SLOT=0: file 1 resumed, AstroGalaxy/1; writes the card: back it up), `opening` (FileSelect/1: the title
test's new file, then the garden's opening: A at each picture book page and at Peach's letter, `opening test f N: nerve
K` for each PrologueDirector nerve (1 Wait .. 9 GameStart, 0 dead once the player has control)); `killer` also freezes a bullet with R2 |
| Host tests | `wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh` |
| Sound packs (after adding SE names to game.c) | per scenario: `for n in 1 2 3 4 5 6; do python tools/sound.py --pack data/fs/files out/packs/EggStarGalaxy/$n/sound.bin engine/src/se_ids.h 16000 --scene out/packs/EggStarGalaxy/$n/scene.bin engine/src/game.c; done` (SYSTEM / PLAYER sounds always, the rest when a placed class may play them (sfxcut's index), Mario's table entries game.c plays; caps scaled to fit 1.85 MB; waves cached in out/sound_cache); the shared fallback without `--scene` to out/packs/sound.bin; also writes engine/src/se_ids.h and mario_snd.h; one SE to WAV: `python tools/sound.py data/fs/files SE_SY_COIN` -> out/sound |
| Coverage (task 5.3) | `build.sh cover out/slice/hd2.bin BattleShipGalaxy/2` then `tools/coverage.ps1` → `out/coverage.csv` (one ELF, the scenario through `out/boot.txt`; ~1 min each); `python tools/class_coverage.py` → `out/class_coverage.md` (behaviour / static / missing placements) |
| Disc image | `build.sh MODE out/slice/hd2.bin Galaxy/n disc` (→ `out/disc/SLICE.ELF`) then `python tools/disc.py` → `out/smg-ps2.iso` (819 MB, all 136 scenarios' packs); the full ISO does not read in PCSX2 (a DVD: see "Disc build in PCSX2"), a CD-sized one does: `tools/run_quiet.ps1 -Iso test.iso [-Elf disc ELF]`; a boot menu lists every scenario (D-pad, X; the build's scenario starts after 10 s) |
| PCSX2 in the background | `tools/run_quiet.ps1 -Elf <elf> -Wait <s>`, `tools/snapbg.ps1 <png>`, log `Documents/PCSX2/logs/emulog.txt` |

Patch scripts go through files (Write), not bash heredocs, because of apostrophes. In Python-generated C, write `\n` as `chr(92) + "n"`.
The OBJS row format (`ROW` in `tools/slice.py`, `Row` in `engine/src/blob.h`) changed to 92 B (switches): any change
there needs the embedded blob and every pack rebuilt.
- Texture layers (feat/multitex, 2026-11-01): 2295 of 7780 materials sample textures on two or more coordinates;
  they were baked with the extra ones at their mean colour (but tex.env_layer's additive env maps, 927). By how the
  extra layer enters the TEV (scratch classifier): environment map added 1125 (909 env_layer), environment map as a
  lerp factor 216 (L(C0, C1, env): the Observatory's metals), second UV set added 102 / multiplied 85 / other 105
  (Good Egg's egg planets x EggHigh, OtaKing's bath, the skies' galaxy / star layers, glows), Mario-style alpha terms
  read with the lighting 109 (left), env map multiplied 59 / other 100, projections ~150 (IndDummy screen copies
  left), unused 79. Now tex.layers fits each extra coordinate (the TEV at 0, 0.5 and 1 over the base's texels) to an
  added / subtracted, multiplied (Cd x As) or alpha-lerped pass; slice.model_groups draws it as a group over the same
  triangles (its UVs through its texture matrix and BTK, VU1Env normals or Projmap), the base baked at its level;
  render.c blends 6 (Cd x As, no alpha test) / 7 (Cd - Cs). Over a VU1Strip base a pass shares its strips (VUMD fmt
  byte 6: per batch only its UVs or normals; slice.strip_batches extra, render.c build_chain_pass). Coordinates made as
  the base's are baked texel for texel (tex.texgen_key). Offline over ObjectData: 1839 of the 2295 draw their layers
  (927 env_layer + 912 new: 609 added, 30 subtracted, 146 multiplied, 187 lerped), 456 stay at the mean.
  Cost, Good Egg bench: EE 15.90 ms either way; VU1 timed (vu1Instant off) 21.80 -> 22.14 ms (44.2 -> 43.6 fps);
  texture uploads 179 -> 183, none without room; vram_need +100-160 KB a scenario. Packs: EggStar 1 whole 11.74 ->
  12.00 MB, EggStar 3 stream 5.12 -> 5.32, HeavensDoor 1 stream 6.56 -> 7.28, garden 3.62 -> 3.71. slice_all tries
  every layer, then the larger ones only (LAYER_STEPS), at ratios down to LAYER_RATIO 0.7, then none (and fits are
  counted in whole sectors, as zone_fit). The Observatory (AstroGalaxy, TridentZone, room 7.71 MB) fits no layers
  even at 0.7 and stays as before (now at ratio 0.9 for scenario 1): next, passes over lit (indexed) bases sharing
  their vertices too, smaller pass textures, or a larger PACK_ROOM (the disc ELF leaves 5.8 MB more). Its floor rings
  (blue inside gold, where a black hole was drawn) come from the same-texgen baking. Seen in PCSX2: the
  skies' star fields (garden, Good Egg, Gateway), Good Egg's asteroids and egg planets. route EggStar 1 star at
  f 3781. ponytail: a coloured layer multiplied / lerped by its grey; a pass takes its base's vertex colours; the
  base's alpha cut-outs do not mask its passes. **Release: full repack** (slice_all.py, then zone_fit / vram_need);
  the engine reads old packs as before, new packs need this engine.
- The new game's opening (feat/prologue, 2026-11-01): StorySequenceExecutor's cDemoPrologue (step 8) on a new file
  (overwriteGalaxyNameAfterLoading) starts PrologueDirector (game.c pro_*, its nerves and constants as Petari):
  ProloguePictureBook ("PrologueDemo") and PrologueLetter ("PeachLetterMini") in menu.c from prologue.bin (tools/lyt.py
  PROLOGUE, main.c loads it in PeachCastleGardenGalaxy), the scenery DemoLetter and the dummy DemoPeachCastleGate
  (slice.py: PrologueDirector in LOGIC, its EXTRA models, the dummy skinned, both canms as "prologue/<name>"), Mario
  at the dummy's MarioPosition joint playing DemoPeachCastleGate, STM_PROLOGUE_01 / 02, the wipes and the cinema frame.
  Every scene now starts on the start's camera (CameraDirector startStartPosCamera at init; ended when the player
  moves with the game camera current over 30 frames): the garden after the opening looks as on the Wii.
  To rerun: lyt.py (prologue.bin), slice_all.py PeachCastleGardenGalaxy, its sound.bin (SE_SV_PEACH_OPENING_LETTER,
  SE_DM_ARRIVE_CASTLE_STAR). Also: the HUD hides in programmable demos too (startDemoSystem). ponytail: submitLevelSE / permitLevelSE; DemoPeachCastleGate's Dark_v_x vignette left out
  and its GateLight's edges solid (VU1 batches keep no vertex alpha: slice.py DROP_MATERIALS).
- Rosalina's storybook, rest (fix/story-rosetta, 2026-11-04): her model was drawn as "the screen through a tint"
  (blend 3, no texture): slice.py took every material sampling IndDummy for CrystalCage's refraction, but Rosetta's
  IndDummy stage is lerp(TEXC, CPREV, A1) with C1's alpha 1 (erase.brk fades her through it): tex.samples_named now
  leaves such a stage out (only Rosetta and RosettaReading change: the dome's Rosetta was invisible too). Also
  ANIM_MAX_DRW 160 -> 224 (RosettaReading 219 draw matrices; Koopa 197, OtaRock 170) and ANIM_MAX_JOINTS 48 -> 72
  (Koopa 70, Rosetta 49): arena +~380 KB. RosettaChair at the sheet's PosName during the demo with its collision off,
  RosettaReading / TicoReading dead until the demo, RosettaReading killed at its end, TicoReading's ColorChange by arg0
  (slice.py BRK_ARG) and random start frame, overlayWithPreviousScreen (render_cover: the next n frames not flipped;
  also GrandStarReturnDemoStarter's 2), lyt text boxes' horizontal position (setTextBoxHorizontalPosition*, lines
  aligned as nw4r: the book's last text centred). RosettaPictureBook (the table's book: A, ロゼッタ絵本デモ, the
  PictureBookLayout's contents mode: Open, ContentsSelect, PictureBookCloseButton, Close). Test: `library` (with
  XF=-DRPB_ONLY=1 only the table's book: contents, chapter 1, B on its title page). ponytail: the circle wipes are fades.
- Observatory / Gateway leftovers of #143 (feat/astro-mapobj, 2026-11-05): AstroOverlookObj (the HUD off while Mario
  is in an AstroOverlookAreaCylinder: slice.py LOGIC keeps both rows; plain build XF=-DOVERLOOK_TEST AstroGalaxy/1),
  AstroRotateStepA / B / AstroDecoratePartsA turn only once revived (isAlreadyRevival by
  getNumGrandStarForRevival) with their engine level SEs, RunawayTico Guide0's TICO_WAIT / FLOAT_DEMO / zoom-out SEs
  and BGM_MEET_TICO_ZOOM_OUT, HeavensDoorDemoObj's Vanish (cage Break, PlanetPartsA's BGM, kill at Move's end) and
  the cage's ミニ太陽消失 shake / rumble, PlanetPartsA's SUN_WORK. SCENE_MAX_TEX 1088 -> 1280: slice_all now gives
  AstroGalaxy layer passes (1121 textures; the old limit made its zone fail to load). Not seen: ミニ太陽消失 (only
  after scenario 1's opening demo, not reached by a test). ponytail: AstroMapObj's Dead / Alive / Revival / Open
  looks (a second model per object does not fit the Observatory's pack), name plates, AstroCountDownPlate's digits.

- KuriboChief (feat/172-kuribochief, 2026-11-04): Enemy/KuriboChief.cpp as walker kind WALK_KCHIEF (its params,
  sensors at its joints, Stagger 240 / kick from 70, BlowDown / BlowDownLand, the kick by walking into it, the star
  piece scale reaction: AnimScaleController asc_*), KeySwitch.cpp's owner-made key (okeys: カギ出現 40 frames, Appear
  bounce, Wait; slice.py KeySwitchOwned = the KeySwitch archive skinned for Rotation), ExterminationChecker.cpp's
  ExterminationKuriboKeySwitch (exts: its zone's ChildKuribo, the key 30 frames after the last is down; now LOGIC, no
  longer drawn as a Kuribo), ChildKuribo (Kuribo with no item, MODEL_ARC / ANIM_ARC Kuribo) and the 全滅用クリボー stomp
  combo (m_combo: SE_SY_TRAMPLE_COMBO port 11, a 1-up from the 8th). Walker kit: ws_find takes mJumpStartStep, ws_stagger
  mStaggerTime / mRotateRateDegree. Test `kchief` (HeavensDoorGalaxy/1, XF=-DPACKS_OVERLAY=wt-audit; -DKCHIEF_KICK the
  kick): the opening demo ended at once, the camera on the chief: it finds and chases Mario (hurt), a spin staggers it,
  a second blows it down: killed, its key bounces out and is taken; the ChildKuribo stomped, the checker's key taken.
  ponytail: BindStarPointer (2P pointer), move limit collision, the camera register vector, the マリオ連続踏み counter,
  OneUpBoard, ExterminationCheckerKuribo (the power star kind), tryAvoid. The chief waits until its ground's collision is
  in (a zone streamed low has none; walkers' zone_kcl gate let it fall through). Repack: HeavensDoorGalaxy (slice.py).

- BegomanBaby (feat/begoman, 2026-11-04): BegomanBaby.cpp in begospike_update (`baby`): its values, sounds, Trample,
  Blow to its death with 1 / 3 star pieces, the rebound between Babies. Test `begoman` with XF=-DBEGO_BABY
  (BattleShipGalaxy/1): it keeps its distance / pursues / turns, a trample makes it Trample (Mario bounces). Left: its
  launches (BegomanBabyLauncher, BossBegoman). Repack: none (BegomanBaby already skinned).
- Mario's offence cylinder (fix/mario-offence-spine, 2026-11-04): mario_touch / mario_offence / mario_kick measure from
  _2A0 (m_center: 60 up, 40 squatting; Spine1 hanging) with cylinderPushCheck's two halves (down along _4C4, then up
  along _4B8), so low sensors (BegomanBaby's body 20 up) are spun. `begoman` XF=-DBEGO_BABY: the spin blows three
  Babies; route EggStar 1 star f 4135.
- Walkers' ground gate (fix/walker-ground-gate, 2026-11-04): w_zone_ground: a walker runs once its zone is full (not
  low: a low zone's base-pack pieces gave zone_kcl without its planets) and has collision; KuriboChief's own kcl_query
  guard dropped. kchief (HeavensDoor 1) lands and is spun; route EggStar 1 star f 4135.

- BossBegoman (feat/191-boss-begoman, 2026-11-05): BattleShipGalaxy 3 / 4's ElectricUfoZone boss after Petari
  (game.c bbego_update, walker kind WALK_BBEGO, its 23 nerves; BossBegomanHead's 8 as hd_*): PreDemoWait, the
  ボスベーゴマン登場 demo (FirstContactDemo, the "OpeningDemo" anim camera, stopStageBGM(60)), Ready (MBGM_BOSS_01_A,
  Recover), the head's switch (trample: OnWeak 420 with its alarms; spin while weak: Blow into an electric rail:
  ElectricDeath, star pieces 8 / 16, JumpToInitPos), its followers (5 Kuribo slots after its own, `host`: 3
  BegomanBaby, 2 BegomanSpike from its last life, launched with BG_LAUNCH), the power star at its place. Test:
  `begoman` build with XF="-DBBEGO_TEST", BattleShipGalaxy/3: demo, 3 electric hits, dead, `power star 1 got`.
  Packs: slice.py EXTRA BossBegoman (its head, the followers' models), SKINNED_OBJ BossBegomanHead, declare_stars
  BossBegoman. ponytail: the head's BRKs, ROT_NEEDLE's volume, overlayWithPreviousScreen, the boss's
  sendMsgEnemyAttack to walkers other than Begomans.
- TripodBoss (feat/246-tripod-boss, 2026-11-05): Megaleg after Petari in engine/src/tripod_boss.inc (#included by game.c,
  tb_*): TripodBoss's nerves, its 22 bones (body + per leg IKJoint root / middle / end and their separated axes),
  TripodBossLeg's nerves (Up, MoveToLandingPos, StampSign, Landing on the MovableArea sphere, LeaveOut under a pressed
  Mario), body weighting, step points / sequences / TripodBossStepStartAreas, every part on bone * inverse(its
  TripodBossBaseJointPosition) (FixParts, RotateParts, Core, Shell, the KillerGenerater singles), the followed gravities
  (records moved each frame, gravity_reset) and switch cubes (area_inside takes Mario back to their design place:
  tb_unfollow) by slice.py's new FOLW chunk (Obj_ID), hidden until SW_A / near (the low model far off), the 開始 demo
  (StartDemo BCK on TripodBoss.bdl's Body / legs joints, its CANM, siren, MBGM_BOSS_02_A), the KillerGenerater(Circle)
  cannons with their HomingKillers, shell / core / guard wall (ダメージ demo, 2ndDemo camera, star pieces, 02_B), the 破壊
  demo (EndDemo, BreakDown, Explosion), the grand star (GrandStar now an ITEM_STAR, declared). Test: plain build,
  XF="-DTB_TEST", TriLegLv1Galaxy/1 (D:/smg-ps2-wt/audit-tb.sh): seen the opening demo (740 frames), the steps of
  sequence 2 / 0, the body with its cage shell, Mario on its top.
  The rest of the fight (#285, 2026-11-05): the killers chase at any angle (initWithoutIter keeps cChaseStartAngle
  360; it was 180), GoToTarget on the shell / core sensors, the guard wall parts' collision per part (the KCL's band
  r 739..800, y 0..440, +-22.5 degrees: requestBreak of the part hit, Mario pushed out), the core broken by a killer on
  its collision, the parts placed again after a rebind (tb_rebind_place). Seen in PCSX2 (same test): an under cannon's
  killer breaks the shell, the damage demo, the guard wall up, a wall part broken, a killer through the gap into the
  core, PainDemo / BreakDown / Explosion, the grand star's appear demo and `power star 1 got`. The grand star's appear
  demo used to hang (the demo moved only the "GrandStar" class; update_items is the "PowerStar" one: dx_cls_moves
  maps a GrandStar to PowerStar, every galaxy's grand stars). Test only: the upper cannons' killers dive round the
  body on its gravities (segment g40, prio 1006, Distant 2150) and hit its side, so the test sets one chasing over the
  top through a gap. ponytail: TripodBossBreakMovement, CameraTripodBoss*, leg shadows, DummyDisplayModel,
  KinokoOneUp, cannon clips, the BreakDown camera (shows the planet close up), the killers' shadow on a cage
  (tripod_boss.inc header).
- Koopa Vs1 (feat/300-koopa-vs1, 2026-11-06): Bowser after Petari in engine/src/koopa.inc (#included by game.c, kp_*):
  Koopa / KoopaFunction / KoopaParts / KoopaSensorCtrl (its 27 sensors at Koopa.bdl's joints), KoopaSequencerVs1,
  KoopaBattleStairsVs1 (registerStair: each KoopaBattleMapStair's mFireAttackStep / target, game.c kss reg / fstep / ac;
  the stairs stop in demos but the requested ones), KoopaBattleMain with its ActorStateKeeper (a nerve change kills the
  old state) and the states Guard / AttackHipDrop / AttackShockWave / AttackFireShort / DamageEscape / JumpAway,
  KoopaSubSequenceBattle, KoopaDemoJumpToPlanet / PowerUp, KoopaFireStairs / FireShort / ShockWave / PlanetShadow,
  KoopaSwitchKeeper / PowerUpSwitch (LOGIC rows), KoopaBattleMapPlanet with its damage / coin / plain plates (insts made
  in game_rebind_: kp_insts), the sheet demos with the Koopa clip and its CANM (koopa/<clip>), the actor cameras
  (クッパ大王（ＶＳ１）), the grand star at デモ中心. Test: plain build XF="-DKP_TEST", KoopaBattleVs1Galaxy/1
  (D:/smg-ps2-wt/koopa-b.sh / koopa-r.sh): SW_APPEAR, the stairs demo and battle (the climb's switches 21 / 22 set by the
  test), its SW_A, クッパＶｓ１本戦開始, the jump to the planet, three levels (hip drop on a damage plate, tail spin, body
  spin) with the power up demos, クッパＶｓ１ダウン, `power star 1 got`. Packs: slice.py (EXTRA KoopaVs1: KOOPA_PARTS and
  the planet / plates, SKINNED_OBJ parts, CANM_ARC koopa, LOGIC keepers, declare_stars, anim_skipped other battles'
  demo clips), jpa.py NAMED MeteorStrike / KoopaPlate*. ponytail: no face control (ActorJointCtrl), no shadows / bloom,
  no screen blur, the plates' / planet's BRKs, the binded collision's sensor is the plate under its feet, anim cameras
  without interpolation, no attacks on other actors, pooled skinned models share one Anim.
  Vs2 (#311): KoopaSequencerVs2, KoopaStateAttackSpin (its AttackSpin sensor), KoopaStateAttackFireLong, the Vs2 planet
  (KoopaBattleMapPlanetVs2, its damage plates only, the grand star plate the first). Test: KP_TEST build with
  KoopaBattleVs2Galaxy/1 (koopa-b.sh KoopaBattleVs2Galaxy/1 "" 2): クッパＶｓ２本戦開始, three levels, クッパＶｓ２ダウン,
  `power star 1 got`. Vs3 not started (KoopaSequencerVs3, BattleVs3Lv1 / Lv2, StairsVs3, DemoFallToPlanetVs3Lv2 / Lv3,
  ChaseRoll / AttackRoll, FigureBall, RockBreak, RestarterVs3, ViewSwitchKeeper, the Peach / KoopaJr / ship demo casts).
- BossKameck (feat/kameck, 2026-11-05): Kamella after Petari (game.c bk_*, its sequencer Vs1 / Vs2, BattleDemo,
  StateBattle's 12 nerves, MoveRail, BattlePattarn) in StarDustGalaxy 2's ThrowTutorialZone: dead until SW_A (the
  SwitchCube), カメック登場Vs1 (the DemoBossKameckStart anim camera on BossKameckBattleDemo, MBGM_BOSS_04, Mario
  BattleWait at MarioPosition), three levels on its BossKameckMoveRails with the beam patterns 2 1 / 3 1 / 4 1 3,
  hit only by a JetTurtle (its turtle beams leave KameckTurtles: trample one, take its shell, spin it at her), star
  pieces 8 / 16, the PowerUp demo bringing its two ChildKamecks, ボスカメックダウンLv1 and its power star. Kameck.cpp
  as walker kind WALK_KAMECK (FireBallBeamKameck, ChildKameck), KameckBeam / KameckFireBall / KameckTurtle pools.
  Test: plain build, XF="-DBK_TEST", StarDustGalaxy/2 (D:/smg-ps2-wt/audit-bk.sh). Packs: slice.py (EXTRA BkDemo /
  BossKameckFireBall / Koura, SKINNED_OBJ ChildKameck / BkDemo, CANM_ARC bkdemo, LOGIC BossKameckMoveRail, MODEL_ARC
  ChildKameck, declare_stars), jpa.py NAMED BossKameckBeam / BossKameckFireBall. Vs2 (#298): OceanPhantomCaveGalaxy 1
  (GhostShipCaveZone), BK_TEST: opening demo, BossKameckBarrier up (its model and collision at the boss's placement,
  game.c bk_barrier_inst), four levels, the PowerUp demo, down, the barrier gone, `power star 1 got`; the boss's clips
  by its class (bosskameck2/...; Vs2 played none before), clip keys over 31 chars shorten demobosskameck to dbk
  (slice.py clip_key, bk_act: Start2 / PowerUp2 / Down2 no longer meet Vs1's). Also: a GrandStar's appear demo
  (dx_cls_moves) fixed in #288; Koopa / KoopaJrShip (not ported) leave their grand stars undeclared (out at once).
  ponytail: the barrier's Wait BRK / BTK, no Suso / Cap dynamic joints, no
  shadow, no star pointer bind on Kamecks, NonActive by distance.
- IceMerameraKing (feat/304-meramera-king, 2026-11-06): Baron Brr after Petari in engine/src/ice_meraking.inc
  (#included by game.c, mk_*; walker kind WALK_MERAKING), IceVolcanoGalaxy 1's IceMountainZone: its 17 nerves, its
  ThrowingIce pool (MoguStone.cpp: IceManIce on a ParabolicPath), IceMerameraKingShockWave (its Shock ring, sensors by
  its joints' scales), IceMerameraKingBreak, the メラキンオープニング demo (by the arena's SwitchCubes: BeginDemo's
  throw, 怒りデモ at part 2, 死亡デモ at part 4; its CameraTargetDemoActor through game.c dx_tgt), the pinch BGM
  (AudBgmConductor playPinchEventBgmLevel / stopPinchEventBgmSoon / movement: game.c bgm_pinch*, bsub.id), its two
  ChildIceMeramera (dead until AngryDemo2nd's step 90, killed in the death demo), its power star from its place.
  sound.py --pack now reads the .inc files game.c includes (tripod_boss.inc's SEs were left out). Test: plain build,
  XF=-DMK_TEST, IceVolcanoGalaxy/1: opening demo, three spin storms / spins while it rolls, both angry demos, death
  demo, `power star 1 got`. ponytail: SE_DM_LV_MUTE_BGM's BGM mute, BodyIceOff's particle callback, its BRKs; its
  children are still the improvised Meramera walker (Meramera.cpp, 1765 lines, not ported).
- Meramera (feat/319-meramera, 2026-11-06): Meramera.cpp after Petari in engine/src/meramera.inc (mm_*, walker kind
  WALK_MERAMERA: Meramera, IceMeramera, ChildIceMeramera), replacing the improvised walker: its 18 nerves, keep-height
  over its shadow, chase / dash / turn, the attack sensor (fire / freeze), spin storm Damage -> Runaway (rolling, jumps,
  diving into a fire floor: Sink / Float / Ignition), Down / FlatDown, star piece / fireball / explosion / invincible,
  ReadyRestart (arg2), head effects on its Sin joint, its BTPs. IceMerameraKing's children use it (mk_child_kill: its
  kill). Test: plain XF=-DMM_TEST HellProminenceGalaxy/1 (chase, attack success, spin: Damage, Runaway, Sink, Float,
  Ignition, Chase). ponytail: BindStarPointer, BRK colours, element attack on enemies. MK_TEST (main too): `power star 1 got` at the king's first damage.
- FallingSmallRock (feat/306-falling-small-rock, 2026-11-06): MapObj/FallingSmallRock.cpp in game.c update_fsrock
  (HellProminenceGalaxy 1 / 4): Wait for SW_A, Move (its FallingSmallRock effect, SE_AT_LV_FALLING_ROCK) until its
  binder (10 at 800 up) touches the map. Rows kept by slice.py LOGIC, its effect by jpa.py NAMED. Test: plain build
  XF=-DFSROCK_TEST HellProminenceGalaxy/1 (SW_A on at frame 300: 13 move, one bound and dead).
- Kingfin / Bonefins (feat/303-kingfin, 2026-11-05): engine/src/skeletal_fish.inc (sf_*: SkeletalFishRailControl,
  SkeletalFishJointCalc, SkeletalFishBaby for SkeletalFishBaby / ChildSkeletalFishBaby rows on the SnakeFish model,
  ExterminationSkeletalFishBaby: TearDrop's 全滅スター出現 star) and skeletal_fish_boss.inc (sfk_* / sfg_*:
  SkeletalFishBoss with its Param.bcsv levels, rails by RailIDLv, 13 rail-bent joints, 16 joint collisions as
  invisible insts (slice.py KCL_PARTS SkeletalFishBoss / SkeletalFishBossHeadA), head sensors, mouth nerves, the
  出現 / パワーアップ / ２ / 死亡 demos on its four canms (CANM_ARC sfb), demo guards, break model, MBGM_BOSS_09_A / B and
  the near / far states, SkeletalFishGuard / GuardHolder). A thrown JetTurtle hits them (sf_jet_hit). Clip keys over 31
  chars shorten skeletalfish to sf (slice.py clip_key, game.c sf_clip). Tests: XF=-DSF_TEST TearDropGalaxy/1, XF=-DSFK_TEST SkullSharkGalaxy/1 (shells
  thrown at the fish by the test): three babies and the checker's star; the whole fight to its star. ponytail: see the two files' headers (BRK colours, shadows, bloom, bird louse,
  columns, 2P pointer, the after-demo programmable camera).
- SubmarineSteam / SubmarineVolcano (feat/307-submarine-steam, 2026-11-06): MapObj/SubmarineSteam.cpp (both names one
  class, the effect group its name) in game.c update_msteam: Steam (SE_OJ_LV_MARINE_STEAM_OUT, _S in SkullSharkGalaxy)
  or WaitForSwitchOn; its strong hit (sphere 120 at 600, cylinder 480, Mario's radius x 0.7 + 20). slice.py LOGIC,
  jpa.py NAMED. Test: plain build XF=-DMSTEAM_TEST SkullSharkGalaxy/1 (both hit Mario). ponytail: group clipping.
- KoopaJrShip (#302, 2026-11-06): Bowser Jr.'s airship after Petari in engine/src/koopajr_ship.inc (#included by
  game.c, kj_* / dkj_*): KoopaJrShip's nerves on its rail, propellers / screws, cannon shells and main shells, its
  HomingKillers, the two Kamecks, 5 JetTurtle hits (Damage / PowerUp / BreakStart / Break), its demo casts (appear,
  turn to the front, Koopa Jr.'s ShipBattle talk as its cast クッパJr: slice.py DEMO_CAST_ALIAS), the burn / move level
  SEs, its power star; KoopaJr on its obj joint; DemoKoopaJrShip (FloaterOtaKingGalaxy arg0 1, TriLegLv1Galaxy arg0
  0: the entry clip and anim camera, the talk, FlyAway). slice.py: SKINNED_OBJ, EXTRA, MODEL_ARC / ANIM_ARC, TALK_NAME,
  declare_stars. Models looked up again and anims rebound in game_rebind_ (kj_models). TicoShop's goods (#318's keep_anim pool) bound again when a
  zone swap moves their model (KoopaJrShipLv1's TLB miss in anim_pose / draw_model_ after BattleLv1Tmp1Zone came in).
  Test: KJ_TEST build with KoopaJrShipLv1Galaxy/1 (Mario to the SW 15 SwitchCube, the switch written after 300
  frames: the cube's area does not write it there, A through the talk, a JetTurtle hit every 300 frames): opening
  demo, the talk, 5 hits, the mid demo, the down demo, `power star 1 got`. DKJ_TEST (FloaterOtaKingGalaxy/1, plain):
  the entry demo by SW 67.

- Dodoryu (feat/301-dodoryu, 2026-11-05): Major Burrows after Petari in engine/src/dodoryu.inc (#included by game.c, dd_*):
  its eight states (Wait, DemoOpening, Lv1, DemoAppear, Lv2, DemoAngry, Lv3 with sDodoryuChaseDataLv3, DemoDown) on their
  own spines, DodoryuSub's MoveStateHolder (Rail / Chase / Detour by the DodoryuClosedCylinder / Escape / JumpOut), the
  MogucchiHill of 50 DodoryuHill pieces (Lead / Wait / Fade, broken by hip drops, tramples, star pieces, spins and its eye
  sensor), DodoryuLeadHill (joints 1-17 turned by _90), DodoryuRabbit (its rail escape, Jump / Return, its talk), the four
  demos' anim cameras (dodoryu/<name>.canm), BGM_PINCH_3, MBGM_BOSS_05_A / B, 8 / 16 star pieces, the after-boss BGM and
  its power star. Test: plain build, XF="-DDD_TEST", CosmosGardenGalaxy/2. Packs: slice.py SKINNED_OBJ Dodoryu* +
  EXTRA, LOGIC DodoryuClosedCylinder, declare_stars Dodoryu, TALK_NAME DodoryuRabbit, clip_key ddlh/; jpa.py NAMED
  DodoryuHill; sound.py now reads the .inc files game.c includes. ponytail: fur, shadows, BTK / BRK / BTP, banmt,
  image effects, the eye sensor on map objects, the sudden-death wipes (dodoryu.inc header).

- Air (feat/332-air, 2026-11-06): Air.cpp's In / Out (arg0 x 100, + 20 x 100 out), its Disappear hide (DarknessRoomAir's
  59 frames; DimensionAir / TwilightAir have none: once shown they stay), arg1's wait for the opening camera, AirFar100m's
  far clip (10000) for DimensionAir; update_airs moves in demos (registerDemoSimpleCastAll). slice.py AIR: no collision
  (DimensionAir's kcl was put in), each baked at its resting colours (own BRK, Appear's end). Drawn: DimensionAir (the 2D
  rooms' streaked backdrop), TwilightAir, DarknessRoomAir. Test: plain XF=-DAIR_TEST (TeresaMario2DGalaxy/1,
  PhantomGalaxy/1: Mario and the camera put at the first one). ponytail: Appear / Disappear fades (no BRK at run time);
  HomeAir, SphereAir, SunsetAir, FineAir stay undrawn: their alpha is GX_LIGHT2's N.L at the camera per vertex
  (LightFunction.cpp:193-195) and VU1 batches have no vertex alpha (drawn flat they flood the screen); so no PriorDrawAir.
  Repack: slice_all.py the galaxies with DimensionAir / TwilightAir / DarknessRoomAir.

## Next

**In-engine demos (the user, 2026-10-17: "haz las dos cosas, primero los videos": the movies are done (3feb30f), now
the demos): resume here.** State at the session break (2026-10-17 evening):
- Done, tested, committed: the generic DemoExecutor.
  - tools/slice.py `demo_chunk`: a DEMO chunk per scenario: every DemoGroup (demoobjinfo) with its sheets from
    ObjectData/DemoSheet.arc (Time / SubPart / Player / Camera / Action / Wipe / Sound), part names resolved to
    indices, its casts (rows whose DemoGroupId = the group's l_id in its zone, DemoSubGroups merged), the GeneralPos it
    names, and `hide` (the appear casts that wait dead: `demo_dead_classes`, MapObjActor's subclasses and a few more,
    read from vendor/Petari). rows() keeps DemoGroupId / CastId / cls; scene() returns demos and gpos.
  - tools/camera.py: "e:<DemoName>[<PartName>]" event cameras (class = actor_hash of that string, num 0).
  - tools/lyt.py COMMON + "CinemaFrame"; menu.c `menu_cinema` (Appear / Wait / End), `menu_wipe` / `menu_wipe_force` /
    `menu_wipe_closed` (black / white fades).
  - engine game.c "DemoExecutor" section (before game_pause_permitted): dx_init / dx_update (DemoTimeKeeper,
    SubPartKeeper, PlayerKeeper, CameraKeeper with camera_event_start, ActionKeeper types 0 1 4-7 12 13, Wipe, Sound
    SEs), started by the group's SW_APPEAR edge or dx.req, waits for !in_opening / no star or talk demo and for its
    casts' zones streamed in full (game_zone_sets: dx_zones_full, max 120 frames); cinema frame, HUD off, Mario waits
    (dx.bck: startBckPlayer), SW_DEAD at the end. row_demo_hide hides casts until their appear. Hooks
    (dx_cast_hook): GCaptureTarget appear (SE, shown at step 30), ElectricRail nerve (disappear).
  - `demo` test (build.sh demo, EggStarGalaxy/1, -DDEMO_TEST: frame 1300 demo 0 "Gキャプチャー出現"): BlueStarZone
    streamed in, camera on the star planet, the five pull stars appear, cinema frame, HUD back after 120 steps.
- Done, tested: StorySequenceExecutor's demo sequences. sequence.c: the Petari tables (SeqDemo), the queue
  (seq_demo_current / step / next / clear), decide_move_galaxy (dome lecture, star piece retry, butler fortress
  reports), decide_clear_galaxy (grand star 1 + Rosetta explain, EggStar 1/2, torch lecture...), after-loading Rosetta
  talk. game.c story_update (ST_PLAY / WAIT_DEMO / WAIT_SAVE / FADEIN_AFTER), started at scene init; the star return
  demos are skipped (`unported` list) until StarReturnDemoStarter / GrandStarReturnDemoStarter are ported; menu.c's
  save prompt waits for game_story_busy().
- Done, tested: ドームレクチャー１ in full. Demo talks (action types 8 / 9 / 10: dx_casts' talker, talk_force; getDemoType
  2 / 3 in a time keep demo, 2 pausing the time keeper until termTalk); DemoTimeKeeper's pause (dx.paused: the keepers
  stop, the talk anims go on); scene DemoTalkAnimCtrl (dx_talk_anims: the cast's clip per part with createBckCtrlData's
  loop frames, a 会話 part pausing 2 steps before its end until the talk is over, the cast's .canm camera from the
  demo's start); casts put at an action's PosName (dx_moved); Butler (SKINNED_OBJ, registerDemoCast by name:
  slice.py DEMO_DIRECT_CAST; its direct talk labels: msg.py DIRECT; dead / ButlerMap appeared by SpecialStarGrand2);
  TicoDomeLecture (Wait / Move eased to (0, -70, 0) / Metamorphosis, then its dead switch: the blue star back);
  AstroDomeBlueStar killed by the demo turns its appear switch off. DEMO parts / sub parts carry the 会話 and
  会話アニメループ flags (padding bytes: old packs still load). AstroDome / AstroGalaxy repacked (AstroDome/1 sound).
  `story` test (build.sh story, AstroGalaxy/1: a fresh file moved to AstroDome/1, X held every second): the three
  parts, the Butler's pages, the Tico's flight and change, the blue star, バトラー情報Ａ passed. ponytail: the
  TicoDomeLecture's ColorChange frame 1 drawn as Tico's 0; the Butler's own talks / nerves (exeWait, star piece
  reaction) not ported.
- Done, tested: ドームレクチャー２ (AstroDomeGalaxySelector::tryStartLectureDemo at the select's start until 天球儀レクチャー,
  at the confirm until ギャラクシー移動レクチャー; requestStartTimeKeepDemoWithoutCinemaFrame: dx_request(name, frameless));
  Butler's startDemoDomeLecture2 (its talk Butler001 / 002: setDemoTalkMessageCtrlDirect = dx_talk_set) and
  exeDemoDomeLecture2 (posed at the camera, facing it); Butler / ButlerMap dead while the galaxy select runs
  (trySyncKillMsgSelectStart: butler_sync). `story` test goes on: R2 at the blue star, the lecture's talk, 天球儀レクチャー
  passed. The `dome` test passes both lectures first.
- Done, tested: StarReturnDemoStarter (game.c srd_*) for 天文ドームスター帰還 / パワースター帰還: ReturnDemoRailMove on its
  rail (ResultFly, ResultFlyEnd at the brake frame, the turn to mForward), FlyWaitPowerStar, Fall to the floor, Land,
  ResultWaitStart / ResultWait, the result window (menu.c holds it: menu_result_start) at the wait part's last step with
  the time keeper paused, FadeOut when another demo follows (seq_has_next_demo), then the demo goes on; the dome's
  kill / appear by the sheet. Mario's demo clips loop as their BCK says. slice.py: the starter cast by the demos' names,
  PowerStar / GrandStar as its EXTRA models; its placement not drawn. AstroDome / AstroGalaxy repacked (+ sound).
  `result` test (EggStarGalaxy/1): the flight in, the landing, the result, the save, then ドームレクチャー１ (the
  test's file has not heard it). ponytail: no grand star flight, the star drawn still (its ResultFly clips not played),
  no SpinTico / MarioHair models, no path drawer / DemoFlyLightDemo effect / BGM.
- Done, tested: バトラー報告 (Butler::startDemoButlerReport: its talk by the story event 4..8, the report's canm camera,
  バトラーリセット: killIfBatlerMapAppear; ButlerMap out and back by isButlerMapAppear; cast by name in slice.py).
  `report` test (EggStarGalaxy/1: EggStar's star 2 faked: the star return, the result, バトラー報告 "¡Bien hecho,
  valiente explorador!", the save prompt). Left: AstroDomeSky's demo nerves (ReturnDemoWait / Disappear: its BRK fades).
- Done, tested: the star piece lecture. スターピース解説前半 (Butler::startDemoStarPiece1: the lecture on, its SW_B, the
  sensor 100; the StarPieceGroup paired by GroupId appears at スイッチ起動 (its SW_A); バトラーリセット: resetStatus),
  Butler::receiveMsgPlayerAttack (a shot at its Body sensor, the StarPieceShooter's lock-on: the pointer's ray within the
  sensor aims the shot at it), ButlerStateStarPieceReaction (StarPieceReaction clip, SE_SM_BUTLER_ABSORB, its talk
  Butler017 / AstroGalaxy_Butler000 chosen at init; no talk at the 5th hit: SE_SY_STAR_PIECE_STOCK_MAX and
  スターピース解説後半 requested), tryReplaceStarPieceIfExecLecture (120 frames with no piece: the group back),
  startDemoStarPiece2 (forceKillStarPieceAll, the group killed, the blue star's waitAppear), exeDemoStarPiece2 (終了's
  last step: スターピースレクチャー passed). StorySequenceExecutor's save after a list ending in 13 only when the sequence
  has not saved (_B0: StartSaveAfterSequence / WaitSaveEndAfterSequence): menu.c's own prompt removed (it saved twice).
  `spiece` test (EggStarGalaxy/1: EggStar's star 1 faked, the dome lecture heard: the return, the result, the save, the
  lecture's first half, five lock-on shots with reactions, the second half, スターピースレクチャー passed).
  ponytail: the pointing SE (isStarPointerPointing1P 弱) and the star piece counter layout / guidance
  (requestStarPieceLectureGuidance, requestCounterLayoutAppearanceForTicoEat) not done.
- All 136 scenarios repacked with DEMO chunks (2026-10-17), menus.bin with CinemaFrame.
- Done, tested (2026-10-18, merged from agent branches): the generic DemoExecutor pieces (d82b4d9): the action sheet's
  AnimName (anim_start on the cast), the camera sheet's anim cameras (dx.acam; slice.py DEMO_CANM bakes every demo camera
  target's canms), the sound sheet's BGM (bgm_start / bgm_stop / bgm_return: tools/music.py --table writes bgm_tab,
  audio_fade; game_bgm_stage is the stage BGM start), the scene stopped during a time keep demo (lq_cls gates
  game_update's update_* by class: the running demo's casts and dx_always_moves keep moving), the start request queue
  (DemoStartRequestHolder: dx_push, dx_busy). `shell` test (HellProminence 1: the LavaShellTower's three anim cameras,
  the BGM stop then MBGM_GALAXY_02_HURRY). ponytail: one class per update_*, the programmable demos do not stop the
  scene, no camera interpolation back, no BGM fade-in / sequenced BGMs, 円ワイプ as the fade, no startPart.
- Done, tested (ffb173b): Rosetta's Observatory demos (RosettaDemoAstroDome.cpp): the talk demos (EntryDemo talkers
  AstroGalaxy_Rosetta300..., her eventFunc), ロゼッタ状況説明デモ (Rosetta's DemoWithButler talk anim with its canm,
  ButlerExplain's ForActor one: only the scene ctrls pause the time keeper; the picture book RosettaMonologue in menu.c,
  PrologueStarSteward in map.bin), Rosetta / ButlerExplain skinned, her WaitA / WaitB idle. `rosetta` test
  (AstroGalaxy/4: 状況説明 with the book, then 天文台機能回復). ponytail: no TurnJointCtrl / talks outside demos, her
  balloon at her Chin's bind height, no BGM events, the book without typewriter / IconAButton, AstroCountDownPlate's
  digits. Seen: AstroGalaxy runs ~35 fps in that demo's camera (not investigated).
- Done, tested (agent-grand merged): GrandStarReturnDemoStarter (game.c gsr_*: ReturnDemoRailMove with brake 296 / turn 98,
  FlyWait, RushToCore at コア中心 from the GPOS chunk, Revival with the DemoAstroReturn<n> canm, the starter's actor
  cameras, the result inside the demo, fade out), StarReturnDemoStarter's grand star branch, the stars skinned with their
  ResultFly clips (PStar / GStar EXTRA models), the Luma (SpinTico) at the landing, AstroCore Revival<n> / Grow,
  AstroDomeSky's nerves with its BRK fade as a black quad. `grand` test (HeavensDoorGalaxy/1: a faked grand star 1 ->
  AstroGalaxy/4: グランドスター１帰還, the result, then ロゼッタ状況説明デモ; HeavensDoorGalaxy/2: パワースター帰還 in
  AstroGalaxy). ponytail: no MarioHair / cap hiding, no star effects / BGM / overlay. Seen: PCSX2 runs the revival's
  whole-Observatory views at ~40 % speed (EE-side logic + draw ~8 ms: the emulator, to check on the PS2).
  Only the DemoAstroReturn<n> canms of the grand star demos placed in a scenario are packed (DEMO_NAMES), and Rosetta's
  DemoWithButler canm only with ロゼッタ状況説明デモ.
- Done, tested: the Butler as a talking NPC (talkcls 5: its placement's talk, AstroDome_Butler000 / AstroGalaxy_Butler012,
  NPCActor caps 150 / 240, Butler::messageBranchFunc as the talker's branch func (Talker.branch: mBranchFunc(mNextIdx)),
  only in its Wait nerve; Talk / Wait clips). Fixed: the dome (class AstroDome) waited dead because a demo makes it
  appear (MapObjActor's rule), but AstroDome::init makes it appeared: the dome's floor and walls were missing (Mario
  fell through) outside the star return. `butler` test (AstroDome/1: Mario before the Butler, X: its talk).
- Done, tested: the story's "just" checks (gamedata.c gd_scene_start at game_init, gd_flag_is_on_just / can_on_just,
  gd_green_first_just: getSceneStartGameDataHolder), so decideNextEventForClearGalaxy queues cDemoKinopioRescued,
  cDemoTrickCometLecture, cDemoLuigiRescued, cDemoGreenStarGetFirst / Release and the KoopaBattleVs3 talk by them (to
  AstroGalaxy), and AstroGalaxy's scenario 4 is grand star 1 just got. バトラーマップレクチャー (ButlerMap skinned with Wait /
  Talk / DemoButlerMapLecture, startLectureDemo, the map at マップ表示開始's end with the time keeper paused, resetStatus;
  its talk AstroGalaxy_ButlerMap001) and バトラーグリーンドライバ説明 (startDemoButlerReport: Butler005 / 006 by the event,
  tryStartShowGalaxyMap at event 8). `astrodemo` test (TriLegLv1Galaxy/1: grand star 2 -> its return, the torch lecture,
  the map lecture with the map, the save; BattleShipGalaxy/1: the first green star -> the star return, the green driver
  explanation, the save, the fade in). ponytail: the map opens in its normal mode (no NewDomeDiscover /
  ChallengeGalaxyDiscover modes).
- **Resume here (2026-10-18 evening):** points 1 and 2 of the release list done and merged (agent-jumpout, agent-ending,
  agent-memcard, agent-map, agent-subs, agent-luigi). Tested in PCSX2: grand (AstroGalaxy/4 loads), dome (jump out,
  scenario select), luigi (talk, his star), luigiplay (Luigi in EggStar/1), prologue (subtitles), title (saves),
  map (open, list, close), ending (EpilogueA, demo, EndingA, staff roll). Fixed on the way: sound.bin's ADPCM freed from
  EE RAM after its SPU upload (main.c se_compact / pk_shrink, ~1.74 MB in every scene) and the room raised 9.01 ->
  10.4 MB (PACK_ROOM, slice_all ROOM; the Observatory was simplified to 0.7 to fit, now full); ButlerExplain packs only
  DemoWithButler; a save of another size (older format) is written over, not NAND_07; KoopaBattleVs3's clear decided
  before the optional demos. Not checked: rumble (needs a real pad), the memory card windows (MC_FAKE builds), the map's
  discover modes / GalaxyDetail picture, the PS2 browser icon. Only AstroGalaxy, AstroDome, EpilogueDemoStage and the
  Luigi galaxies (PhantomGalaxy, EggStar, BattleShip, HoneyBeeKingdom) were repacked with the new room; the rest fit as
  they were. Left: the sound packs for Luigi's SEs (sound.py --pack), a release build + ISO (disc.py: subs.bin,
  letter.bin, mappics.bin, resident_luigi.bin, the Luigi movies), then the test campaign (point 3), phase 1, sound.
  - Done, compiled only (agent-subs, 2026-10-18): movie subtitles and pad rumble. MovieSubtitles (movie.c subs_*:
    sSubtitlesInfo, CinemaSuper, Talk / TalkWait / next page, two 60 Hz steps per movie frame) drawn over the picture
    (render_picture's overlay, under the wipe) from out/packs/subs.bin (tools/lyt.py build_subs: CinemaSuper, the
    Layout_StoryDemo messages, only their MessageFont26 glyphs (msg.py font_subset_chunk), MRUM: MoviePadRumble*.arc);
    main.c movies_play loads it at the bottom of the pack region as `scene` for lyt.c / msg_get (msg_set) / the talk
    font (game_font_init), render_reserve_blocks keeps the picture's VRAM. Rumble: pad.c WPadRumble (eight channels,
    gRumblePatternTable, the 1800-step rest) on the DualShock's small motor (RUMBLE_BIG: the large one's level, 0),
    stopped at scene changes and pause; DemoPadRumbler over the movies; wired in game.c where Petari calls it (Mario's
    startPadVib, launch stars, OtaKing, DinoPackun, CocoSambo, HammerHeadPackun, TicoFat, switches, pickups, ...).
    Before testing: `python tools/lyt.py data/fs/files` (writes subs.bin beside menus.bin). Tests: `prologue` (Koopa's
    subtitles from ~65 s into PrologueA), `finalmovie`; rumble on a real pad (PCSX2 shows nothing without a rumble
    pad). ponytail: trample / spin rumbles at mario_offence (not at the message taken), no ActorPadAndCameraCtrl (the
    ActorInfo BCK rumbles), no DemoCtrlBase / StageEffectDataTable / SimpleEffectObj rumbles, no swim / slider / Foo /
    flip / ceiling rumbles (not in the port), no second WPadRumble for the pause menu, the movie text in MessageFont26
    (CinemaFont26 not converted).
- Luigi (agent-luigi, 2026-10-18), compiled only, untested: gamedata.c FindingLuigiEventScheduler (LuigiEventState
  starts at STATE_NULL 0xFF00 now: gen_gamedata.py), the EventUtil Luigi checks (gd_luigi_*), each file's Mario and Luigi
  game data (mario%d / luigi%d, the last loaded player: the save image doubled, MC_MAX 48 KB: older card saves are
  dropped); sequence.c updateOnStageResult / update per move, cDemoLuigiMissing and getOptionalDemoForClearGalaxy in both
  paths, Luigi's data starts at HeavensDoorGalaxy; game.c LuigiNPC / LuigiEvent (talkcls 6: Afraid, Normal, Event,
  OnTree, Arrested nerves, TakeOutStar as the star's declarer), KinopioAstro talks (talkcls 3) with the messenger's
  letter event, talk branches 10 (isPlayerLuigi) and 14 (Luigi results 12 / 13), MarioConst table 1 for Luigi
  (mario_const.py: MC_TABLES, MC = *mc_table); menu.c LuigiLetter (letter.bin), the pause menu's LetterButton, BrosButton
  and FileSelectInfo's Mario / Luigi state; slice.py resident_pack(player="Luigi") -> resident_luigi.bin (Luigi.arc as
  "Mario", luigiAnimeSwapTable clips), main.c loads it instead of resident.bin for Luigi's file and letter.bin where the
  letter can show; movie.c plays <name>Luigi.smv (FinalBattle, EpilogueA, EndingA) for Luigi, disc.py takes them.
  To rerun: lyt.py (letter.bin, title.bin with BrosButton), slice_all.py (resident_luigi.bin; PhantomGalaxy, EggStar,
  BattleShip, HoneyBeeKingdom and AstroGalaxy repacked for LuigiNPC / LuigiEvent), then the disc. Tests: `luigi`
  (PhantomGalaxy/1: Mario before Luigi, X talks; his star), `luigiplay` (any scene played as Luigi).
- Pack memory (2026-10-18): the Observatory ran out of pack region ("packs: no memory for ...": the heap is full after a
  load). slice_all.py's room per galaxy now leaves out the menu packs main.c loads beside it (MENU_PACKS: map.bin in
  AstroGalaxy, dome.bin, the title packs), and slice.py anim_skipped leaves out clips / canms a class never plays in a
  stage (the Butler's dome demos outside AstroDome, Rosetta's other stages' demos in AstroGalaxy, ButlerExplain's copy of
  the Butler's). main.c prints the pack region's blocks when it runs out.
- Next, in order: GrandStarReturnDemoStarter and the grand star flight; Rosetta's demos (RosettaDemoAstroDome.cpp, the picture book); anim cameras of the camera
  sheet (AnimCameraName); more dx_cast_hook classes (engine classes with type 2/3 casts: EyeBeamer, doors via their
  switches, CollectTico, FirePressureRadiate); BGM changes of the Sound sheet; the other actors stopped during demos
  (requestStopSceneFor).

**Menus and game flow (2026-10-17, before the demos):**

**(the user, 2026-10-17: "quiero que metas todo esto que falta ... para que el release sea lo mas
fiel al original"): resume here.** Everything outside the levels, after Petari (all of it is decompiled: ~35k lines;
two tables are compiled into the DOL, not on the disc: cGalaxyInfoTable, cStoryProgressTable) with the disc's
LayoutData layouts. Order by dependency:
1. Done fd5b997: scenes change in place (game_request_scene; engine/linkfile .gamedata / .gamebss reset, pack region).
   Done a287978: runtime layouts (tools/lyt.py -> out/packs/menus.bin: LYTR + TEXT + MSGS; engine lyt.c) and the pause
   menu (menu.c: ButtonPaneController, YesNoController, SysInfoWindow, PauseMenu, START; `pause` test: EggStar 1 ->
   pause -> Volver al Planetarium -> Sí -> AstroGalaxy 1). Pointer: the right stick, X decides (A). Left for the pause
   menu: the star row (game data), Luigi's letter, save before End Game, IconAButton; the stage title / counters'
   follow positions (setFollowPos).
2. Runtime layouts: tools/layout.py converts a layout's pane tree (pan1 / pic1 / wnd1 / txt1 / bnd1: transform, alpha,
   visibility, materials) and its BRLANs (pai1: RLPA / RLVC / RLVI pane tracks, RLMC / RLTS / RLTP material tracks,
   Hermite and step keys); the engine plays them like LayoutActor / LayoutManager (MR::startAnim per channel,
   MR::startPaneAnim on a pane's subtree, frame control, isAnimStopped), panes found by name (MR::showPane /
   hidePane / setTextBoxMessage...), windows' nine pieces at their animated size, text boxes from messages.
3. Messages for menus: a resident table of the System / Layout / PauseMenu / FileSelect / Scenario / Galaxy names
   labels (tools/msg.py), CustomTagProcessor's tags (numbers, pictures, player name).
4. Widgets: ButtonPaneController (Appear / Wait / SelectIn / SelectOut / Decide / End), IconAButton, BackButton,
   SysInfoWindow (Key / YesNo), wipes (WipeRing / WipeKoopa / fades), SaveIcon.
5. Progress and save: GameDataHolder / GameDataAllGalaxyStorage / GameEventFlagTable / GalaxyStatusAccessor (the
   ScenarioData.bcsv fields), the two DOL tables extracted (Petari config/RMGE01 config.yml extract: offsets in
   main.dol), save on the memory card (libmc) as SaveDataHandleSequence asks; host builds a host: file.
6. Sequence: GameSequenceProgress / StorySequenceExecutor (the next stage after a star, a miss, game over),
   LogoScene (strap / censorship screens), Title (TitleSequenceProduct: TitleLogo / PressStart), FileSelector (the
   FileSelect stage: files, Mario / Luigi icons; Mii parts left out).
7. In a galaxy: GamePauseSequence (PauseMenu, PowerStarList AllStarList), miss / game over (TooBad, GameOver wipe,
   PlayerLeft), CometRetryButton, GameStageClearSequence after a star.
8. The hub: AstroGalaxy / AstroDome objects (AstroDome, AstroDomeSky, AstroDomeGalaxySelector, MiniatureGalaxy,
   AstroChangeStageCube, AstroCountDownPlate), ScenarioSelectScene + ScenarioSelectLayout / Star (ScenarioSelect),
   StageTitle, StarReturnDemoStarter + StageResultInformer (ResultWindow), GalaxyMapController (Map* layouts).
   Done (this commit): ScenarioSelectLayout / ScenarioSelectStar / BackButton in menu.c (menu_scenario_select from
   seq_galaxy_move SEQ_SCENARIO_SELECT with no scenario: the stage left frozen, the sky and SelectPowerStar /
   SelectPowerStarEmpty from out/packs/menus3d.bin, X picks, Circle / Triangle or Volver cancel back to the stage, a
   white wipe into "<galaxy>/<n>" and a white open there: seq_set_open_wipe). `scensel` test: EggStar with star 1 ->
   Star2 -> EggStarGalaxy/2. Fixed on the way: lyt setAnimFrameAndStop is rate 0 (isStop: ended or rate 0; ScenarioSelect
   Appear never played), other languages' panes stay hidden (lang_off), simplify.py rounds its target (at 0.99 every
   small mesh lost a triangle: the select sky's quad; **every pack needs a repack** for it and for slice.py VIS
   PowerStar: frame 0, the clear shell no longer drawn over the stars).
   Done (next commit): the dome's galaxy select (game.c dome_*: AstroDomeBlueStar as gcap 4, SphereSelector, Handle,
   MiniatureGalaxy (+Holder index, AstroDomeOrbit rings), AstroDomeCameraController, AstroDomeGalaxySelector; menu.c
   menu_dome_*: GalaxySelectInfo, GalaxySelectBackButton, GalaxyConfirmLayout, GalaxyNamePlate). The blue star's
   appear switch is forced on (its lecture demo not ported). slice.py: Mini<Galaxy> draw their own model plus
   MINI_EXTRA (the factory listed the unknown one first); AstroDome repacked (slice_all.py takes a galaxy filter).
   `dome` test: grand star 1, R2 on the blue star, Good Egg opened (New), chosen, Start, Star1 -> EggStarGalaxy/1.
   A cancelled scenario select reloads the stage left. Left: galaxy pictures (ObjectData/GalaxyInfoTexture, 42 x
   256 x 72), IconComet, star number plates, the demos (lectures, Mario's jump out), comets.
   Done (next commit): the miss / game over sequences (game.c miss_event_update: EventDown, EventGameOver; menu.c
   TooBad, WipeKoopa, GameOver layouts; lives kept in the game data over scenes). Release builds (CATCHUP) and the
   `miss` test (EggStar: a miss reloads the stage with a life less, a game over goes to FileSelect/1).
   Done (next commit): title + FileSelect (menu.c fs_*: TitleSequenceProduct, FileSelector's ring, camera, sky,
   FileSelectItem planet / fellow, FileSelectNumber, FileSelectInfo, FileSelectButton Start / Delete, BackButton,
   create / delete Yes-No; whole-screen like the scenario select). tools/lyt.py writes title.bin + title3d.bin, loaded
   in FileSelect only. gamedata.c keeps six files (gd_file_*: create / delete / load / save / info, gd_image for the
   card). `title` test: A+B (X+Circle), file 1 new, Yes, Start -> PeachCastleGardenGalaxy/1. A release disc boots
   FileSelect/1 (build.sh ... FileSelect/1 disc). Left: icon select (Mii / fellows), Copy, Luigi, the date, the
   PicLogoGalaxy mask (hidden), blink / morph effects.
   Done (next commit): the memory card (mc.c: rom0 MCMAN / MCSERV, mc0:/SMGPS2/save.dat = gd_image, read at boot,
   written in steps polled by main.c) and SaveDataHandleSequence (menu.c sv_*: System_Save00 / 07, Save01 >= 120
   frames, Save02, ConfirmEndGame remind) after a stage clear (sequence.c save prompt, 90 frames into the next scene:
   untested), the pause menu's Save and Quit in the Observatory (untested), game over (tested: miss test).
   Done since (compile-only, untested in PCSX2): icon.sys + save.icn (tools/ps2icon.py from SaveIconBanner.arc
   SaveIcon00.bti on a flat quad; assets.S links them, mc.c writes them with a new save), save.dat with
   MR::calcCheckSum after the image, port 1 when port 0 has no card, NANDErrorSequence's windows in menu.c sv_*
   (boot: corrupted -> NAND_07 / System_Save05 / System_Save04; no card, unformatted, no room -> NAND_02 /
   System_Save04 -> play without saving; a save's failed write NAND_11_2, its no card / no room NAND_02; "the Wii
   Menu" is OSDSYS BootBrowser). Fake a card with `XF=-DMC_FAKE=MC_NOCARD|MC_FULL|MC_CORRUPT|MC_FAILED`. Left: the
   Wii texts (no PS2 strings on the disc), file select saves (fs_) show no System_Save01 window, browser icon unseen.
   Done (next commit): StageResultInformer (menu.c sr_*, layout ResultWindow): the star got with the galaxy and
   scenario names (message string arguments put in by msg_with_args), the count up, a new coin record, the star
   pieces into the stock; then the save prompt. sequence.c keeps the cleared stage's result (seq_set_result at the
   star's touch). `result` test: a faked EggStar clear -> AstroDome/1, the result, record, pieces, save.
   Done since: galaxy pictures in the dome's GalaxyInfo (dome.bin), file select Copy, the pause menu's follow
   positions, the galaxy map (menu.c gm_*: GalaxyMapController normal modes, GalaxyMap Astro / Grand Galaxy views
   with DomeIn / DomeOut, galaxy icons by MapPaneName (GDAT), dome icons, hover name plate, GalaxyDetail, MapButton ->
   PowerStarList pages; map.bin in the AstroGalaxy stages; opened by ButlerMap's talk (talkcls 4, untested in play);
   `map` test opens it directly). All packs repacked 2026-10-17 (simplify / PowerStar VIS fixes; 136 fit).
   Left on the map: discover / Tico / dome-lecture modes, Tico / comet / Mario icons, the list's belts / page turns,
   GalaxyDetail pictures (no room in the Observatory: AstroGalaxy streams 8.4 MB).
   Done since: MovieStarter (the prologue: a held fade for each THP movie, PrologueB -> HeavensDoorGalaxy 1; `prologue`
   test), comets (gamedata.c GalaxyCometScheduler: Petari's six timetables, sync at each move, forceToNextState after a
   stage result; the dome's confirm -> the comet's scenario decided, the scenario select's comet warning; `comet` test).
   The save format is v2 (comets): v1 saves are dropped. The disc ISO (1.15 GB) still does not boot in PCSX2 ("open
   fail SLICE.ELF;1"): OPL on the PS2 should load it; PCSX2 runs out/slice-packs.elf with host: packs.
   Done since: icon select (MiiSelect fellows, `title` test goes through it), the dome info's comet window, the save
   window over the pause menu, the pause test answers Save and Quit. Release rebuilt 2026-10-17 15:40 (out/smg-ps2.iso,
   out/slice-packs.elf boot the title).
   Done since (3feb30f): the THP movies. tools/movies.py (ffmpeg) writes out/packs/movies/<Name>.smv (MPEG-2 without B
   pictures + s16 PCM in one-second chunks, ~600 MB for all; disc.py takes them but the Luigi ones); movie.c plays one
   with libmpeg between two scenes (main.c scene_switch, game_request_movie("A,B")): memalign / free wrapped to the
   packs' region (Makefile --wrap), the picture uploaded in macroblocks to the texture cache's VRAM, paced by the EE
   Count at 29.97 fps, sound through audsrv, sInfoTable's open / close wipes, FinalBattle skippable once visited.
   Hooks: MovieStarter 0 / 1 / 2 (PrologueA -> the garden reloaded at start 1, PrologueB -> HeavensDoor 1, FinalBattle
   -> the Observatory reloaded with KoopaBattleVs3's scenario select), the KoopaBattleVs3 clear -> EpilogueA, EndingA
   (+ EndingB at 120 stars) -> the title. Tests `prologue`, `finalmovie`. Release + ISO rebuilt (ISO 1546 MB).
   Left: subtitles (MovieSubtitles), pad rumble, EpilogueDemoStage's demo and the staff roll between the movies.
   Next: the Observatory / dome demos (butler lectures, AstroDomeComet, Mario's jump out), comet / Tico icons on the
   map, Luigi; then phase 1, sound.
Then phase 1 (Good Egg vs Wii: the rim step's ground check, below), sound last. The disc ISO over ~1 GB does not boot
in PCSX2 (taken for a DVD: open fail; a CD-sized image boots); the user's way of loading it on the PS2 was asked.

**Release push (the user, 2026-10-16 night): resume here.** Order: phase 0 (done: 92f184e; videos published by the
user), phase 3 (all galaxies), phase 2 (60 fps), phase 1 (Good Egg against the Wii), sound last (may be disabled for
now). Port everything first, **no PCSX2 / route / coverage runs until the porting is finished**; every commit builds
and says it is untested. Phase 3 steps: repack every galaxy (the 8 stale 84 B GRAV packs), then the top static
classes of `out/class_coverage.md` (regenerated 2026-10-16: behaviour 84.9 %, static 8.8 %, missing 1.0 %) after
Petari, Lumas / NPCs, then the misses from the 2026-10-10 coverage run. To check in the test campaign (append as
you go): route EggStar 1-4 after the air-velocity change (92f184e); OtaKing's hit-back coconuts pass ~650 over Mario
on the rim (ota_ball_aim, Petari CocoNutBall::setVelocityToPlayer _BE: compare with the Wii).
Ported in the push, all untested (build only; check each in PCSX2 in the test campaign, in a galaxy that has it):
Pole / PoleSquare / NoModel / TreeCube (b504c11: grab, climb, handstand, jump off), Dossun (54857c5: press, carry),
RingBeamer (5028a31: group rings, JumpBeamerBeam Spread frame), SearchBeamer (0deccc6: beam joints, stop when stood
on), Gesso (ec36be2), BasaBasa / Ice (92fee2b), Jellyfish (3d84949: rail), Kanina / Red (c202214: blue 1-up given
at once), Mogu (520592e: stone, hole), Mogucchi (58e507c: rail, mounds), Metbo (2f37776), WhirlPoolAccelerator
(0aa7722: swim push, bands; jpa.py texture 8), WaterPressure (d741cb8: bullets catch Mario), Trapeze (b23375d),
Onimasu / Pivot (e6b407e), LavaStrangeRock / L / Table (9372c0f), FirePressureRadiate (5b7a98d), StarPieceMother
(45e2cc7), WaterLeakPipe (9d9fc50: Top joint height), KoopaBattleMapStair (9ea461a), EyeBeamer (15ae558: railer gated
to Patrol, beam model), BlueStarCupsulePlanet / UFO (2c8b4f2: GCapture targets), CoconutTreeLeaf (79cc555: per-leaf
KCL in kcl_query, Hit.sub), PalmIsland (393fe60), TeresaWater (ea6ff0e), FishGroup A-F (8ba6ead: rows kept without a
model), SeaGullGroup (4bec260), KirairaRail (ab0860c), SunakazeKun / TwisterSan (3b537ac: Mario bind, tornado jump
booster), Snakehead (82d7b10), Kabokuri (115d815: KabokuriFire), Pukupuku (44e061d), NeedlePlantFly (06ad5ee),
TypicalDoor family (52504dc: <class>#Open KCL), LotusLeaf (cdd9d14), MeteorCannon / IceMeramera (ec30854),
ChooChooTrain (76cc3cb: car instances, insts_cap) / LavaHomeVolcanoFlow, DesertMovingLand (4221141), BigFan
(56e57a0), ElectricBall (0846644), BombHei / launchers (3f0cbb0, 2a9496d: walker_acls), JumpBeamer (9434051: the
RingBeamer port's rbeamer 2), ArrowSwitch / Multi (110fefc, 15c552a: ArrowSwitchTarget rows kept), AnmModelObj
family (25d882c: 19 classes; MapObjActor collision on the Move joint, SKIN pad 1, Inst.kmove), SpinLeverSwitch /
LavaBreakColumn (f8b655f), ValveSwitch (4bc40ae: the screw port), DesertLandDownSwitch (a6b27c7: '<class>#Move'
KCL parts as invisible instances, Inst.kparent), Torpedo (5412934), TreasureBox* (beca05a: jet turtle / explosion).
Phase 3 porting stopped at 89.9 % behaviour (class_coverage, 2026-10-17); next is the test campaign (repack all,
then PCSX2 per class above), then phase 2. Left as static on purpose: Flag* (a cloth renderer), StringSpider / SpiderMapBlock / SpiderCoin (the
SpiderThread system), MorphItemNeoIce / Hopper (Mario forms), KoopaStatue (explosions only). New walkers go through
new_walkers[] (count / placement / model by class); their star piece reactions in update_shots. Every pack must be
rebuilt before the test campaign (slice.py SKINNED_OBJ / EXTRA / keep list and jpa.py changed; OceanPhantomCave 1
is 9.03 MB streaming, over the 9.01 budget: check after the repack).

**Test campaign (2026-10-17):** all packs rebuilt (slice_all.py repacks a scenario over ROOM at ratio 0.9 / 0.8 /
0.7; all 136 fit). Full coverage rerun on e5aeec4: 118 ok; misses only the old ones (PhantomGalaxy 3 / 6, CannonFleet 1,
BreakDownPlanet 1, EpilogueDemoStage 1); FactoryGalaxy 1 timed out once, ok alone (39 s). Fixed on the way: 38fff06
(starter drop 100 up; DeathArea SW_A / arg0 / launch rules), 290cd04 (no air-velocity rebase after a launch star, from
92f184e), 33a9cd3 (starter flight ends at rest, from d6a62ae). The vine test mode is stale (also on old engines). Next:
route EggStar 1-4, PCSX2 spot checks of the classes above, then phase 2.
Route EggStar 1-4: all four stars (2026-10-17). Note: out/boot.txt (the coverage runner's scene) overrides every
build's scene: delete it after one-scene runs.

**Phase 2, frame rate (2026-10-17):** EggStar 1 bench start view (XF=-DBENCH_STILL) 21.1 -> 16.6 ms avg (worst 25),
scripted path 16.5 -> 13.3. a5a4975: an animation's forever effects (JPA max frame 0) are made once per actor and
kept while the clip plays (Petari createSingleEmitter / SyncBckEffectChecker); Takobo's wait had piled up 200
emitters / 1200 particles; kept effects survive one update without their owner (catch-up); ocean spheres' facing
sort from precomputed triangle normals (the points don't fit the 8 KB D-cache), the face pass reuses the back's
projection (render_mesh_same). 69479ed: draw_skin caches by clip which clips have no effect / sound rows. Left (start
view): walkers 3.1 ms (Takobo ~280 us each: pose ~100 us for 11 joints, the skinned draw's per-group matrices
~175 us; a fast sincos and a binary key search changed nothing measurable, reverted), insts 2.4, ocean 2.0, logic
4.4. Next ideas: the skinned matrices block on VU0 (pvw x drw, world x drw, light) in one pass, worst-frame spikes
(draw max insts 7.6 / pickups 6.0 ms).

**Earlier (2026-10-16 morning, stopped by the user mid-investigation):** a longer Wii comparison, from the start
to the first launch star (the SuperSpinDriver under the start planet, at -4631 -17641 -16400, the bottom of the tower
that hangs under it). tools/dolphin/play/launch1.json: stick (0, -1) from g 90 runs Mario back over the wooden rim;
in the port he wraps to the underside by g 250 (up 0 -1 0) and walks inward until he stops against the tower at
-4094 -14328 -16222 from g 350. The Wii, same inputs (probe.ps1, Mario at 810B59A8): g 350 at -3802 -14219 -16207
(300 off the port), g 600 at -2292 -13628 -16389 (elsewhere). Next: probe the Wii at g 150 / 200 / 250 (with the camera,
8087B1B8) to find where the rim wrap parts (the gravity at the rim or the camera through the flip), fix it after
Petari, then go on down the tower to the launch star (a spin by it) and record both. The replay log now prints Mario's
up. The pipe on the house's roof (-5551 -12109 -17737) leads into the house, not under the planet.
**Phase 1, 2026-10-17 (launch1.json again, Wii probes every 5-10 frames, port `replay` logged every frame):** the
port started walking ~8 frames late: its standing turn (その場足踏み, stand_turn 10) skipped decideWalkSpeed; in Petari
MarioAnimator::update decides it under that clip (turning stays on while it runs), so the index is set at once and
the clip stops. Fixed: the port now matches the Wii within 4 units up to g 125 (Wii start -3303.3 -13081.9 -15352.9 at
g 93, moving from ~g 96). Next divergence: the step down off the wooden rim, g 130-145: the Wii goes down ~5 a frame
(g 125 -13130, 135 -13173, 140 -13196, 145 -13231) and nearly stops after (x -3110 at g 145, -3108 at 150, -3083 at
160), the port drops faster (g 130 -13157, 135 -13218, 140 -13231) and runs on at full speed. Look at doLanding's
branches (isStickFull / _3CA / frontDot > 6 vs the 着地 clip) and whether the Wii is walking down a slope there
(read its animation). 810B59A8 is a position copy, not Mario::mPosition (+0x208 / +0x278 / +0x71C read garbage):
Mario (Petari Mario, RMGE01, this scenario) is at 810B7E88: mPosition +0x130 = 810B7FB8, mFrontVec +0x208,
mWalkSpeed +0x278 (Wii 0.4516 at g 110 = the port's 0.452), mJumpVec +0x2D4, mMovementStates +0x8 (byte 0: 0x80
jumping, 0x40 on ground), _3BC +0x3BC, _3CA +0x3CA, _430 +0x430. The rim step: both on the ground at g 130 at the same
place and walk 0.823; from g 131 the Wii is airborne (0xA0: jumping, walk 0, mJumpVec 5.59 -2.70 6.29), lands ~g 144
and walks off again from 0 (slow start: walk 0.007 at g 145, 0.094 at 150). The port keeps him on the ground down the
steep part (grounded through g 131-150, y falling 10-23 a frame) at full walk. Next: the port's ground check against
Mario::checkGround / checkGroundOnSlope (MarioCollision.cpp 1727, MarioSlope.cpp 50: distance 60, the slope tests),
then the drop (tryDrop: walk 0, the velocity into mJumpVec) and doLanding.

**Night of 2026-10-16 (the user: comparison video, demo, short, then "continua con el resto de los detalles del port
para ps2 hasta las 7 am"):** videos/2026-10-16_cmp-goodegg-final.mp4 (Wii | PS2, same inputs), _demo-goodegg.mp4,
_short-goodegg.mp4, titles and tags in videos/youtube_2026-10-16.md. Done after Petari, each checked in PCSX2 (route
EggStar 1 star, takobo / walker / karikari / ota tests, bench 16.9 ms): 1fa6980 actors move through the scenario
opening and a 156-frame loading pre-roll (the Takobos' patrols match the Wii's RAM), ClippingJudge clipping (300,
near 500, far 10000), SpinLight on the hand; 5b05ea7 fx 256 emitters (the 128 were full: new effects failed
silently); 55ccbbd LensFlare (LensFlareDirector, BrightSun visibility against the map, ring / glow; no GlareLine,
glow without its falloff stage: FLARE_GLOW_SIZE); 8f13321 VROrbit's cloud scroll (its BTK on the Projmap SRT);
2145a5d Mario's common smokes (cSmokeTable); 6158efa JPA stripes (the spin's cyan swirl), LensFlare's two-stage
materials baked (glow falloff, GlareLine back), Mario's animation effects (flight lines, NAMED_ANIMS); 3a86604
surface sounds (KCL Sound_code -> ports 11 / 12; the KCL attribute layout changed: floor 0-5, wall 6-9, camera 10,
sound 11-14, so other galaxies' packs need a repack before their sounds are right). Bench EggStar 1: 18.3 ms (the
flights' effects); 8982dcb / 051a1b0 Mario blinks and plays his clips' eye patterns (updateBlink, setBlink: SWAP and
EYEP chunks); 72e5cac MarioWait (1800 still frames: sleep, then sleeplie). videos/2026-10-16_cmp-goodegg-final2.mp4: the comparison with all of it (cut with
tools/dolphin/align.py). Left from the comparison: the spin light's stripes drawn as sprites,
GlareLine, the glass capsule's env map, the Wii's 16:9 view (the port is 4:3), the ~44 opening frames the Wii may run
under the loading black, the Takobo meeting at g ~780 (a frame or two apart).

**Wii comparison, 2026-10-15 (the user: "compara el primer nivel con la versión de Wii ... gameplay idéntico en
ambos"; resume here):** Good Egg's sounds are all in (613da57) and route EggStar 1-4 still get their stars. The same
input timeline now drives Dolphin (a generated .dtm from boot, Gecko warp to EggStarGalaxy 1) and the port (`build.sh
replay`, host:replay.bin): see docs/compare-wii.md for the method, the calibration numbers and the differences found.
Second side by side (after d6a62ae / 619d977: landing, HUD, opening, welcome, pointer):
videos/2026-10-15_cmp-goodegg-moves2.mp4, same path to g 300 and the same end place; left in docs/compare-wii.md
(Takobo hit, sky look, the start camera at the landing, control timing after it). Old list, done: Mario's landing point after the
ScenarioStarter flight (the stone circle), the HUD (counters, life meter) and the scenario intro (name plates,
Welcome), the flower beds' rim collision, the pointer only when used; then a longer common route (to the first
launch star, SuperSpinDriver row 59 under DiskGarden) and a re-run of the comparison.

**Frame rate, 2026-10-15 (the user: "optimizarlo para que sea 60fps, o minimo 30"; resume here):** EggStar 1
bench (scripted path, `build.sh bench`) 29.3 -> 16.5 ms avg, worst 45 -> 32.6 ms; start view (XF=-DBENCH_STILL)
19.7 ms avg (logic 5.8, draw 12.1, gs 1.2), worst 29.4. Normal builds now run 60 Hz logic with catch-up
(-DCATCHUP, any build without a test mode: up to 2 logic steps a frame, draw-time clips / sounds / wipe advance
those steps): the game keeps its speed at 30 fps; at the start ~29 fps, 60 needs the start view under 16.7 ms.
Done: f0e7798 fx_anim index, ocean sphere sines / facing sort, walkers out of view skip pose / shadow / draw
(anim_advance, render_visible); 88d4bb6 catch-up loop, fx_update gravity per emitter; a420f07 walkers clipped
out of view stop moving (Petari startClipped), kcl_query over a list of collision instances; 6b78f6e mat_mul on
VU0, skinned draw matrices once a draw; 2232d91 BENCH_STILL. All checked: takobo / karikari tests as before, route
EggStar 1 star at the same frame. Bench output: draw phases (setup, skies, insts, pickups = walkers, mario, rest,
fx, 2d) avg and worst, VIF wait (~0: EE-bound, not GS-bound), pose / skin / draw_model, kcl_query, fx_anim,
walker cost by kind, game_update calls over 100 us, rest parts. Start view now: walkers 3.6 ms, ocean spheres
2.8 (render_mesh EE projection, 2 passes), insts 2.7, fx draw 1.5, mario 0.9, draw_model 3.7 total (219 calls, 68
culled), pose 1.6; logic: fx_update 1.16, update_kuribo 1.0, update_mario 0.7, obj_anims 0.5, launched pieces 0.5.
Next ideas: ocean spheres (project once for both passes, LOD by distance), anim_pose sin / cos (euler -> quat per
joint: a table or VU0), clip cache (8 slots thrash: ~7 inflates a frame), fx_update / fx_draw particle cost,
clipping for other actors (Petari: every LiveActor), draw_model per call (light_matrix, chain building), Mario's
logic. prof.c (Timer 1 sampler) does not work: EPC in the handler is the kernel's (0x81fe0).

**Good Egg, 2026-10-15 session (the first goal; only Good Egg until the user says otherwise):** EggStar packs and
resident.bin repacked with the 88 B GRAV. Done and checked in PCSX2: b4686ad zone-swap memory (OceanSpheres, anim
clip info and CocoSambo heads were allocated above the arena mark: route EggStar 1 crashed in draw_oceanspheres; now
`route` EggStar 1-4 all get their stars); 43f8cd1 camera shakes (CameraShaker vertical tasks: Mario hip drop / hard
landing, spin drivers, Dino, OtaKing, OtaRock, CocoSambo with its FallHead / Blow scene stops, HammerHeadPackun,
TicoFat), TicoFat's WipeOut screen effects, the white wipe really white (`fat`, `ota`); e804311 talk cameras
(CameraTalk for CameraNormal messages, 会話 multi-actor for CameraEvent; `talk`); 0002de2 `talk` with
XF=-DTALK_CLS=3 (Kinopio, EggStar 3). TicoFat's 喜び camera needs arg7 6: Good Egg's has -1, nothing to do.
Then bd4d04d PhantomShipBridge (skinned; waits up with SW_A, MoveA / MoveB, its Move collision on the joint:
slice.py KCL_PARTS; `BRIDGE_TEST`; 8f40a79 `orock` puts Mario on the far side: the raised bridge stands between
OceanTwinPirates' OtaRocks) and 184c1a5 environment maps (tex.env_mtx: J3DTexMtx modes 6 / 7 on GX_TG_NRM, VUMD
lit byte 2 + its matrix, VU1Env, render_env; coins, star pieces, spin drivers, pipes, power star, crystal
highlights; IndDummy materials (CrystalCage's refraction) blend kind 3: frame buffer x tint mean). Checked: `talk`
EggStar 3 (the cage is see-through), `purple` (100 coins, the star), `walker`. The first PCSX2 run after a new GS
blend state can stall for a while (Vulkan pipelines compiling): not a hang.
21ec942 env layers (tex.env_layer: PREV + K x TEXC over a base texture drawn as a second additive VU1Env group;
Dino, Tico's eyes, TicoFat, FlipPanel; OtaKing's env stages come first, not covered). The Plant climb pose follows
Petari (base matrix up along the rail, GrowPlantHangUp swings his body out from its frame 18: SMG's spiral climb);
`vine` with XF=-DVINE_CLOSE puts the camera 450 from him.
cfde7b6 layouts (tools/layout.py: BRLYT pic / window panes with their TPL textures, nine-piece frames, alpha down
the pane tree, groups; LAYO in the FONT chunk): TalkBalloonEvent and SignBoardBalloon drawn from them (`talk`
EggStar 1 SignBoard and Tico checked). 6b89acc TicoFat's Info: InformationPlanet in InformationWindow until A
(`fat`). The fullness meter needs BRLAN (StarPieceTargetCounter slides in from x 500 in Appear; the meter's fill
is a masked texture animation): next, a brlan reader (pai1: RLPA pane SRT / alpha at a frame, RLTS texture SRT).
a86f326 the fullness meter (counter at its Appear's end, the meter's frame at the pointer; its masked fill not drawn).
Regression 2026-10-15: `route` EggStar 1-4 all get their stars with every change above.
The meter's fill done (checked in `fat`: right half, then left, clockwise to a full disc in the reticle): layout.py
emits the masked panes (LayQuad fill 1 + k, white texture) and bakes static texture SRTs (the reticle frame: scale 2
mirrored, it drew a quarter); game.c meter_fill turns Meter.tpl by count.brlan's keys at FullnessMeter::setNumber's
frame and draws the half MeterMask shows; render.c Quad vw / uh: skewed quads as ST triangle strips (UV is unsigned:
rotated corners fell off the texture). ponytail: Shine / Flash animations.
The event balloon's beak after Petari (TalkBalloon::updateBalloon / TalkBalloonEvent::updateBeak; checked in `talk`
XF=-DTALK_CLS=3 EggStar 3: it points at Kinopio): layout.py masked panes are generic (any picture whose material has
a mask texture: the rectangle the mask shows through its SRT 1, coordinates before the SRT 0 rotation, its centre and
translation in LayQuad, now 32 B), game.c lay_turned draws them turned (the meter by count.brlan, the beak by
beak.brlan's -50..50 at the Beak frame aimed at the talker, 10-frame hysteresis; hidden off screen or above y 120).
Known: `talk` (class 1, EggStar 1) no longer reaches its SignBoard (it picks row 98, 2800 off, Mario stuck against
the ground: before these changes too); the Kinopio variant works.
Sound effects started 2026-10-15 (phase 0c's design: waves in SPU RAM, the JAudio2 sequences run on the EE):
3d83540 tools/sound.py (an offline renderer after Petari's JAudio2: BAA, BSTN / BST / BSC, IBNK v1, WSYS + AFC,
JASSeqParser / JASTrack / JASChannel / JASOscillator; every Good Egg SE renders; WAVs in out/sound for listening);
e939d89 sndvoice.irx (engine/iop/sndvoice: one SIF RPC a frame with a batch of libsd voice commands, answering ENDX;
ADPCM upload; audsrv keeps its stream), se.c; 0aa40a1 se_seq.c (the sequencer in C on core 0's 24 voices) and
sound.bin (`sound.py --pack`: the SEs game.c names, their waves at caps scaled to fit 1.85 MB, se_ids.h); baa3b5a
Mario's sounds (mario_sound = Mario::playSoundJ over mario_snd.h from MarioSound.cpp; MarioJump.cpp's calls: jumps,
hip drop, landings); d1112e7 animation sounds (.bas of MarioAnime and Good Egg's actors via draw_skin: footsteps,
Dino, Kinopio...). Checked in PCSX2 by voices in use / ENDX (`snd`, `route`, `walker` log `se:` lines), not by ear:
the user should listen. Then (all after Petari's call sites): 769bc7d system sounds (pickups, star, shots, TicoFat's
meter / gifts, talk A button / start / balloon open / close / page), db8ab7a Mario's spins / damage / life and the
launch stars, 5af640d Kuribo, b072583 Takobo / Karikari, 4954408 breakables / ItemBubble / coin pops, 7010b10 sound
packs per scenario (EggStar caps 14-16 / 10-11 kHz), 78c3098 DinoPackun, 7a7d9f1 OtaKing / magma / coconuts (`dino`,
`ota` fights still end with their stars). Next for sound: TicoFat, Tico / Kinopio talk sounds (startTalkSound by
message), KuriboMini, PackunPetit, HammerHeadPackun, Petari, CocoSambo, KillerGunnerSingle, OtaRock, Plant, Butterfly,
NeedlePlant, CollapsePlane, QuestionCoin, BenefitItem, StageEffectDataTable movers, Mario's remaining table keys
(swim, slip, wall, hang...), ports 11 / 12 for the floor's sound code (footstep surfaces), JAIAudience 3D, voice
stealing by priority. References: docs/jaudio2-formats.md (the JAudio2 formats and rules, from Petari's
JAudio2 decomp), docs/goodegg-sounds.md (every Good Egg class's Petari sound calls, Mario's table, the .bas tables). Known: `talk` (class 1) no longer reaches its SignBoard (Mario stuck 2800 off, before
these changes too).
Left for Good Egg: sound (see above), OtaKing's bath
rim crevice (only from a test's artificial drop), the short (stretch) balloon's layout (no Good Egg message uses it).

**Parked 2026-10-14 (the user's call: Good Egg first), resume later:** the coverage-run session stopped mid-work.
f8ff9e6 (untested, committed as it stood): gravities follow their switches (GlobalGravityObj::updateSwitch; SW_SLEEP
not honoured), Shadow / Magnet gravity types left out, negative Distant keeps the default, cancelling pulls give no
direction. GRAV records grew to 88 B (OBJS row): the full repack was killed partway, so the packs of Phantom,
ReverseKingdom, SandClock, StarDust, TearDrop, TeresaMario2D, TransformationEx and TriLegLv1 are still 84 B (stale,
broken with this engine); rerun `python tools/slice_all.py data/fs/files out/packs 6`, run the host tests
(engine/test/run.sh, slice.py selftest), then the coverage run (out/coverage.csv held 110 of 136 when stopped).

**2026-10-14 session (build only, nothing run: the user's call; test all of it first next session):** the video
fixes below were tried blind (b935237: the Plant's front kept orthogonal as makeAxisUpFront does, the only
difference from Petari found statically; demo_star takes the nearest shown star, no 3000 limit; game_demo_ota
picks flat rim ground only), then c's BTK at run time (79e5410: tex.btk, slice.py BTK chunk, anim_btk, VU1 UV
matrix at VU mem 5..7; EggStar packs and resident.bin repacked; OtaKingMagma, ElectricRail, CocoTwin,
DorayakiFortressPlanet scroll), drop shadows (a9c2f2e: render_shadow disc, draw_shadow after ShadowController,
Mario and the walkers), WipeFade (9ad2d54: TicoFat), actor event cameras (e1a4c0b: camera.py "<actor>固有<event><set>",
camera_actor_start; Dino ダメージ, Plant 出現デモ / 掴まり, TicoFat 変身 / 飛行), animation-driven effects (34f49ac:
AutoEffectList rows with AnimName, fx_anim from draw_skin; resident.bin +95 KB), Dino's footprints (c43f791),
Butterfly colours (5ba9db0: slice.py BRK_ARG, "<class>~<frame>" models), named effects (ca83e30: fx_named;
KillerGunnerSingle, ItemBubble, OtaKing, OtaKingMagma, CocoSambo, Dino's Hit / TailDrag). Good Egg packs and resident.bin
repacked (the disc image is stale). Check in PCSX2: `vine` (Mario's pose on the stalk, its cameras), `dino` / `ota`
TEASER demos (Dino's damage camera, foot smoke), `fat` (wipe, cameras), lava / rails scrolling, shadows on slopes,
no other model's UVs broken (the microcode change touches every draw), particle counts with many walkers.
Checked 2026-10-15 in PCSX2: `vine` (grows, Mario spins up it and jumps off its top, over and over; the camera is
far, his pose on the stalk not judged), `fat` (talks, 100 pieces, its flight camera, wipe, planet D; the flight
camera looks steeply down where the sky dome is dark: black there, the sky is drawn), `ota` (rallies, hits,
LavaColumn drops, smoke), `dino` (fight, camera, the star taken), `walker` (Butterfly colours, shadows on flat
ground), no broken UVs seen. Fixed: JPA tiling (05c34d3, BSP1 bits 25 / 26: UV 0..2 by the BTI wrap; smoke showed a
hard-edged quarter). Seen, not fixed: the TEASER `vine` demo walks Mario into black hole 364 (video only); the
DropOfWater drops are pale capsules (env-mapped material: no texgen); OtaKingMagma is flat pale yellow (its
material / lighting), so its BTK scroll cannot be seen; TicoFat pale (lighting).

**Where the 2026-10-12 session stopped (resume here):** items a and b are done; c's MarioPress is in (build only,
unverified: checkVerticalPress / setPress after Petari, side presses not); c's talk / messages are in for Good Egg
(checked in PCSX2: `talk` EggStar 1 SignBoard + Tico, `fat` EggStar 2 TicoFat's talks to its planet). 60 fps: one
logic step per frame is Petari's 60 Hz (the game ran at half speed at 30 fps); gravity bounding spheres, cached ups
(pickups, walkers), per-type ambient effects and shared plant poses took Good Egg's bench from 23.4 to 12.6 ms
(worst 17.6). Next: the short talk balloon and Kinopio (EggStar 3) unchecked, then the rest of c. EggStar
1-4 `route` got their stars before the a commits below; host tests pass (EggStar packs repacked 2026-10-12, the other
galaxies' are older: item d). The user's last instruction: finish item a **without running the PCSX2 tests** (build
only), so these a commits are unverified in PCSX2: slope slide (550f1d1), Plant groups' clips (members posed by their
nerve's clip, turned at random), QuestionCoin's throw (bound 3 steps at the binder, then its Y x arg0),
CollapsePlane's collision shrink (makeEqualScale's mean), OtaKingMagma's collision + Mario's damage floors (`ota`,
`route` 3 worth a run). Order: first every improvised part, then the missing systems. Left, in order:
  a. Improvised: done. Moved to c, as they need a system: TicoFat / Tico talk (talk), OtaKingMagma's BTK and Rock's
     Size BTK frame (BTK at run time; Rock's only scales its environment map: no texgen in the renderer), the
     walkers' shadows (shadows).
  b. Classes of Good Egg: done (KillerGunnerSingle, ItemBubble, OtaRock, TakoBarrelA, HammerHeadPackun, Plant,
     Petari, PhantomShipHandle / Propellers, StarPieceFollowGroup, CoinReplica, PowerStarHalo / ZoneHalo,
     BenefitItemInvincible). The rest of the EggStar 1-5 classes game.c does not spell are planets (planet table),
     SimpleMapObj / SimpleEnvironmentObj (static or their own-name BCK: DropOfWaterCore, MysteryGravityRoomBridgeA,
     ReverseGravityTowerInside, AsteroidB), generic railers (RGTowerPressGroundA: RailMoveObjPress, only its body
     sensor's press type differs; BroadBeanMoveStepA / B, SideSpikeMoveStepA), areas (camera, sound, bloom, messages,
     light: item c) or need item c: SignBoard / LuigiEvent (talk), DimensionAir / HomeAir (TEV rim material, NO_DRAW),
     ShootingStarArea (RandomEffectObj: random effects in an area). The other Plant grows on its PackunPetit's dead
     switch 4. Next is a.
  c. Systems: talk / messages done for Good Egg (tools/msg.py: TALK per scenario with the reachable FLW1 nodes and
     messages of its MessageId rows and Common_ talks, FONT in resident: messagefont26 / picturefont atlases, UsSpanish
     by msg.LANG; game.c: TalkNodeCtrl / TalkMessageCtrl / TalkDirector / TalkState / balloons / IconAButton after
     Petari, the balloons flat panes at their BRLYT places; SignBoard, Tico, Kinopio, TicoFat use it; ponytail: no
     window textures / beak / brlan curves, no talk camera, no sounds, no yes / no selector, no LuigiEvent),
     BTK at run time, shadows, wipes (WipeFade), actor cameras, animation / named effects, footprints and BRK-by-arg
     colours done 2026-10-14 (unrun; see above). Left for Good Egg: real lighting (TicoFat's pink), bloom models
     (OtaKingMagmaBloom), TicoFat's FatRed / 喜び camera / screen effects, sound. (OtaKing's LavaColumn / AngrySmoke / Appear are animation-driven: fx_anim covers them.) Star piece colours done 2026-10-15 (bc84fdb, checked in
     `fat`: slice.py MAT_COLOR "StarPiece@k" in resident.bin, material colour 0 = initColors[k] plus the environment
     map's mean (no texgen); placed arg3 / random, groups random, StarPieceDirector's in turn). Next: real lighting.
     Real lighting plan (2026-10-15, after Petari LightFunction / LightDataHolder / LightZoneDataHolder /
     ActorLightCtrl / LightUtil / SceneNameObjListExecutor cDrawBufferListInitTable):
     data: ObjectData/LightData.arc LightData.bcsv rows (AreaLightName, Interpolate, Fix, and for Player / Strong /
     Weak / Planet: Light0 / Light1 {Color RGBA, Pos XYZ, FollowCamera}, Alpha2, Ambient RGBA), Light<Zone>.bcsv
     (LightID -> AreaLightName; -1 the zone's default), LightCtrlCube areas (arg0 = LightID, ZoneLightID). The stage
     default (mDefaultAreaLight) = zone 0's LightID -1 name (else the first row, デフォルト). MR::loadLight(type):
     Strong / Weak / Planet from the default area light; Player through its ActorLightCtrl (area by position, blend
     over Interpolate frames, 30 default). Light type per class: its connectToScene* draw buffer ->
     cDrawBufferListInitTable (Planet / MapObj / ClippedMapParts: Planet; NPC / Enemy / Ride / *StrongLight: Strong;
     *WeakLight: Weak; Player / PlayerDecoration / CrystalBox: Player). GX: diffuse clamp, attenuation off (lights at
     1e5 are directional), FollowCamera positions are in view space, else world; channel = ambient + sum lights.
     Engine plan: A) slice.py vu_groups writes per unique vertex normals (V4-8) for lit materials and leaves their
     colours unshaded (lit flag + gain 1 / 2 from bake's RAS 1 / 0.5 choice), BATCH 60 (7n + 6m + 2 <= 496); each
     matrix carries 2 more qwords, the two light directions in its local space (CPU: R^T l), and the draw's light
     colours + ambient sit at VU mem 8..10; vu1_tex.vsm: colour x gain x clamp(amb + sum max(0, n . l_i) col_i).
     B) light type per class (a table from the classes' connectToScene* calls). C) LightCtrlCube areas and the
     player's ActorLightCtrl blend. Then a full repack (VUMD changes).
     Done 2026-10-15 (d9c94d7; A, B, C; checked in PCSX2 `walker` EggStar 1: smooth shading, Mario and walkers lit;
     host tests pass): tools/light.py (LGHT), tools/lighttype.py -> light_types.json (TLGT), VUMD lit byte + normals,
     BATCH 60, VU1Lit program, VU mem: double buffers from 16, UV matrix 8..10, light colours 11..14. Full repack
     2026-10-15 (the disc image is stale). ponytail left: Alpha2 / coin specular / point lights, non-player actors
     switch area at once, bake gain heuristic (RAS 1 or 0.5) still decides the texture's level.
     TicoFat's pink done (d747526, checked in `fat`): slice.py BRK_STEPS "TicoFat~0..3" (FatRed frames 0 / 20 / 39 /
     59), game.c fat_model by setScale. With it: materials whose RAS is CLR0 get per vertex the TEV at their CLR0
     (ras_colour, textures read 1) and the texture divided by that factor at the mean (exact for TEX x f(RAS):
     PeanutPlanet, the egg planets); it changed every pack (full repack 2026-10-15). Non-separable TEVs (OtaKingBath's
     ColdLava adds other textures) keep the bake at the mean CLR0 and CLR0 / mean in the vertices (slice.separable).
     Bloom done 2026-10-15 (checked in `ota`): render.c bloom_pass after BloomEffect (bloom models into the frame's
     alpha only, frame x alpha to a 160 x 56 quarter, sL1 / sL2 blurs, added at the BloomArea intensity; VRAM 112 KB
     for its buffers, vram_need default 2304 KB), game.c bloom_update (ImageEffectArea, 1 / 15 fade, 0.1 approach),
     "<class>Bloom" models (PlanetMap::initBloomModel) and OtaKingMagmaBloom. ponytail: scene colour under the bloom
     models, no threshold, OtaKingMagmaBloom unposed. TEASER vine demo fixed (4d186b8: Mario on the ground beside
     the stalk). The "pale capsule" was DropOfWaterCore (opaque stone); its water is OceanSphere, done 2026-10-15
     (b6cce1f, checked in `swim` EggStar 1, now on its biggest OceanSphere): game.c os_build / os_point /
     draw_oceanspheres after OceanSphere.cpp, render.c render_mesh (EE-projected textured triangles), jpa.py bakes its
     face and env textures into resident.bin (fx_ocean_tex). ponytail: one texture sample for the face's two stages,
     culling by facing, spheres behind the camera or past 30000 skipped. Next: item d (coverage run) and the disc
     image, then the rest of Next.
     MarioPress done (MarioPress.cpp checkVerticalPress, setPress 0 / 4 after Petari: press objects are the
     RailMoveObjPress classes; Dossun / RailBlock / TripodBossFixParts are not in the port; checkSidePressPre /
     checkSidePress need front / back walls and face velocities the port's collision lacks), talk / messages, sound effects, real lighting, wipes, multi-actor cameras (Dino damage camera),
     footprints (FootPrint decals), looping / attached effects (Smoke trails, Glow), BTK / BRK animation at run time
     (lava, water, per-instance colours: Butterfly arg0), bloom models, shadows.
  To fix (seen in the 2026-10-12 video demos, `TEASER=1` builds of talk / fat / vine / dino / ota):
     - Plant (beanstalk) climbing: Mario looks glitched while he climbs / scales it (his pose on the stalk); the
       clip was left out of the video. Nerves, constants and isMsgUpdateBaseMtx match Petari; the front is now
       written back (b935237). If it still looks wrong, compare the GrowPlant* clips' root against Petari's player
       base matrix (the stalk's centre line is Mario's origin).
     - OtaKing's bath (EggStar 3): Mario dropped on the rim ends inside the rock (the crevice below), the camera
       with him: the fight could not be recorded naturally (game_demo_ota).
     - game_demo_dino does not take the PowerStar after the down (demo_star finds nothing to walk to).
  Known: the skinned-object anim pool (MAX_OBJ_ANIM 40) can be full at a rebind, leaving an inst without its Anim
     (in->anim NULL, drawn and posed static); OtaRock re-asks every frame (obj_anim), others should too (KillerGunnerSingle).
  Known: damage floors (Death 1, DamageNormal 4, DamageFire 10, DamageElectric 15) act on the .pa codes: packs older
     than 2026-10-12 (raw .pa indices) can kill or hurt Mario on ordinary ground until repacked (item d).
  Known: KCL prism attributes are now the .pa codes (slice.py kcl_codes: Floor_code bits 0-7, Wall_code 8-11,
     Camera_through 12; collide_n ground_attr); packs older than 2026-10-12 still hold raw .pa indices (item d), so
     their floor codes are wrong until repacked. Mario's slope slide is MarioSlip.cpp (isSlipFloorCode as decompiled:
     all but NoSlip / Sand / water bottoms slip; it is consistent, not inverted); `slip` tests it on DiskGarden's Slip
     floor. The old `slope` mode (Gateway face) is stale: Mario never lands there.
  Known: resident.bin now holds PlantStalk.bti after the WaterPlant textures (jpa.py): the disc image needs it rebuilt.
  Known: `route` (EggStar 1 / 4) logs TLB misses in rail_at (a rail with no points read, p NULL) while it weighs
     launches; harmless there, not traced.
  Known: our collision can hold Mario airborne in a crevice between faces too steep for ground (EggStar 3 bath
     rim, seen in `route`: jumping, nearly still); Petari's Binder would slide him out.
  Known: the camera goes to (0, 0, 0) in some zones (Factory 1 `hip`, CannonFleet 1 `spin`, before part 2 too): XZ_PARA
     there, likely a zero zone matrix in zone_pose's to_world (game_debug now prints camtype).
  d. Full repack of every galaxy (PATH record, FX ids, ANIM / SKIN changes), then the coverage run (task 5.3).

0. Star pointer on the right stick (agreed with the user): cursor, GCaptureTarget, shots, TreasureSpot, freeze done;
   left: pointer effect sprite, railed GCaptureTargets (objinfo rows: no MapPartsRailMover columns).
1. Look: more effects (child particles, rotation, other field / shape types; Mario's dust, on his joints).
   Mario has SpinLight, HipDropFlower and Damage (`mario_fx`).
2. More switch sources and users: Kinopio / Tico.
   More power star declarers (`STAR_DECLARERS` in slice.py, then the origin in `star_pos`): bosses, Tico
   collecting (チコ集め), Kuribo / SkullShark baby "all dead" checkers, NPC events; PowerStarAppearPoint groups.
3. Lumas / NPCs (static models today).
4. More classes by placement count (`out/census.json`, `out/class_coverage.md` top static, 2026-10-09: behaviour 70.9 %,
   static 12.3 %, missing 8.3 % of placements, 2026-10-10): CoconutTreeLeaf (BeeFlowerHover still lacks Bee Mario); SurfRay and Begoman* wait: Petari has
   their constants commented out (no values); SpiderCoin needs the 2D spider threads.
5. Vertex alpha in VUMD (rgbi has no room: the 4th byte is the index) for SphereAir-like fades.
6. Task 5.3 gate: 2026-10-10 run after the classes above: 114 play clean, 10 miss, 0 timeouts, 12 hidden stars left
   out (`out/coverage.md`), worst frame 149.8 ms (CosmosGarden 2). Earlier the same day: 106 clean, 16 miss, 2 timeouts.
   Fixed on the way: Mario starts at MarioNo 0 of zone 0 (Petari cInitializeStartIdInfo(0, 0); scene_start took the
   first start of zone 1, a sub-zone's, in 100 of 135 scenarios); the first load also takes what streaming wants
   around the start, `soon` zones are urgent while Mario is off every full model (OceanRing 4 / 6); black holes leave
   Mario alone in a launch star's flight and a miss ends the flight; the cover test takes a shown launch star landing
   by something that is not an area or a gravity; PhantomShipBridge drawn and collided lowered (slice.py POSE: MoveA's
   last frame, move.kcl at joint Move merged by kcl_tris / kcl_build, a one-block KCL writer: KoopaJrShipLv1 1).
   AstroDome 1-6 fixed (slice.py ASTRO_MODELS: AstroDome / Sky / Entrance / StarPlate take their dome's model by arg0,
   AstroMapObjFunction::getModelName). Left: FileSelect / EpilogueDemoStage (menus);
   SurfingLv2 1 starts on a SurfRay (not done); Phantom 6: the cover test's launch star lands by another scenario's
   planet; boot hangs reading a pack (SandClock 1 / 3 in batches, the fluff build 3 / 3): one big host fread never came back; reads are now 128 KB at a time (3 / 3 fine since);
   single misses after the cover test's flight or by enemies (CosmosGarden 5, StarDust 5, BattleShip 3, CannonFleet 2,
   HoneyBeeEx 1, CocoonEx 1, BreakDownPlanet 1). Earlier: class weights (`out/class_coverage.md`); left: videos.
   Blocking zone loads (a zone wanted before its prefetch ends) are the worst frames (HoneyBee ~0.4 s).
7. Last (the user's call): sound effects (AFC -> ADPCM), music on the disc (streams in the ISO), ChangeBgmCube / RestartCube BGM changes.
- SpiderThread (feat/198-tomb-spider, 2026-11-05): MapObj/SpiderThread*.cpp in game.c sth_* (update_spider_thread /
  sth_draw): made at the scene's TombSpider (TombSpiderEnvironment's initSpiderThread at its place), 8 radial lines x 8
  main points, anchors, 64 circle parts, 1101 points; springs, near-point pulls, wind (battle / chance / battle end),
  pointer plucks (R-stick cursor speed), actor touches, hangs, cut. SpiderCoins bind to their nearest circle part point
  (spider_coin_web). API for SlingShooter / TombSpider: sth_appear, sth_start_battle / _chance / _battle_end,
  sth_start_actor_bind, sth_send_msg (STH_HANG_START / END / APART), sth_hang_pos, sth_touch_actor, sth_try_push,
  sth_pos_z, sth_pause_off, sth_on_bloom; sth.cancel (ACTMES_HANG_CANCEL's actor). Test `tomb` (StarDustGalaxy/3):
  the web appeared, the camera face on, touched every 120 frames. ponytail: untextured strips (no SpiderThread.bti /
  Indirect.bti warp), no bloom pass, the pull level SE without its params, no 2P pointer, getPosZDiff assumed.
- SpiderAttachPoint (feat/198-tomb-spider, 2026-11-05): Ride/SlingShooter.cpp in game.c sls_* (update_sls, Mario's
  sls_try / sls_hold before the SpaceCocoon in the move chain): bound to its web point at load; Mario touching its bind
  sensor (80 at the web's Z) is bound (Land SpiderCocoonLand, Wait), the pointer on it (210 at 50 up) with A hangs it
  (Aim: the pointer on the web's plane, 0.1 a frame, at most 300; the web point follows: sth_hang_pos), A let go 100 or
  more off launches it at 45 (Attack: 120 frames or a map plane after 10; CocoonFly), Square cancels (Spin2nd), every
  nerve, sound and event camera name ("プレーヤー射出者[ウェイト] / [狙い中] / [攻撃中]", camera_event_named). API for
  TombSpider: sls_attacking (its attack sensor, 100), sls_attack_hit (attackSensor after ACTMES_SLING_SHOOT_ATTACK:
  broken / pass through / last damage), sls_enemy_attack, sls_msg (SLM_DEMO_START / BATTLE_START / RUSH_CANCEL).
  Test `tomb` from 400 frames after the landing (XF=-DTOMB_AIM=n frames of aim): bound, Wait, Aim 300 off, launched,
  the flight ends on the arena's inner wall (y -29206 down / -26102 up). (Mario then drifted 2450 out along +Z with
  no planet there: TombSpider's planet collision, below, holds him in the web's plane.) ponytail: Kinopio riders, 2P pointer, the
  drag SE's volume, the cameras' CameraTargetMtx (player-targeted here), map parts taking its attack, shadow.
  Repack: StarDustGalaxy (jpa.py NAMED SpiderThreadAttachPoint, Mario's SpaceCocoonBlur; its sound.bin).
- TombSpider (feat/198-tomb-spider, 2026-11-05): Boss/TombSpider*.cpp in game.c tsp_* (update_tsp / tsp_draw):
  TombSpider's nerves (WaitPlayer: Mario within 6000 starts ゲートオープンデモ; DemoGateOpen: Mario puppeted from 7000
  along Z through the gate to the floor, the web appears, the planet's BattleStart; ActionCocoon: a launch breaks the
  cocoon; 出現 (the planet's Appear CANM "tspdemo/appear"), Battle1stStart (BGM BOSS_03_A), JumpToPlayer; Action1st;
  １回戦終了 (Battle2ndStart CANM, BOSS_03_B at 180, 25 star pieces); Action2nd; 死亡 (Death CANM, the web cut,
  BattleEnd, SW_B, SW_A, the power star from its Body)), TombSpiderActionBase / 1st / 2nd / Cocoon / StateSwoon with
  their constants, TombSpiderSensorCtrl's 17 sensors (eye / hip / glands / vital spots at the player's Z), the sling
  messages (ATTACK / IS_BROKEN / PASS_THROUGH / IS_LAST_DAMAGE: sls_attack_hit), the acid attackers on Mario or his
  attach point (sls_enemy_attack), TombSpiderGland / VitalSpot nerves on their joints (initFixedPosition), the acid
  effects, thread attachers (tryPushSpiderThread on 7 joints). TombSpiderEnvironment's planet is an instance with its
  KCL (slice.py EXTRA TombSpider: planet, cocoon, gland, vital spot, all skinned; CANM_ARC tspdemo; declarePowerStar).
  Test `tomb` (StarDustGalaxy/3, XF=-DPACKS_OVERLAY=wt-boss2; -DTOMB_WEB the web alone): the gate demo, a launch at
  the cocoon, at the hip, the three vital spots, both phases, death, star taken (f ~4400). BossBgmID_TombSpiderA / B
  are BOSS_03_A / B (SoundUtil.hpp). ponytail: BRKs (Battle1st / 2nd colours, the
  cocoon's and vital spots'), BAS sounds, light ctrl, the planet's low LOD, the cocoon's push (Mario moved out of its
  sensor), SpaceCocoonBlur in the gate demo. Repack: StarDustGalaxy (slice_all.py, jpa.py NAMED TombSpider /
  TombSpiderGland / TombSpiderPlanet, its sound.bin).
- BossStinkBug (feat/boss-stinkbug, 2026-11-05): Boss/BossStinkBug*.cpp in game.c bsb_* (update_bsb / bsb_draw): the
  sequencer (WaitStart on SW_A, OpeningDemo, Ground, FlyDemo, FlyLow, AngryDemo, FlyHigh, FinishDemo), every action's
  nerves and constants (ActionBase update*, BossStinkBugFunction moveRail / regainToRail / turnRail / setFallVelocity),
  its rail 0, BGM StinkBugA / B, star pieces 8 / 16 (declareStarPiece 24), its three collisions as instances on the
  Body joint (BodyGround / BodyHipDrop: slice.py KCL_PARTS), hip drops, the shake-off when Mario stands on it, the run /
  fly into Mario (mario_wall_inst: Mario's wall instance), 16 bombs, the demos (pd, Mario at BossStinkBugDemo's
  MarioPosition playing BattleWait, the dummy's CANMs "bsbdemo/*"), the power star from its place, SW_DEAD.
  StinkBugSmall: SW_A ForceFall, SW_DEAD instead of its pieces, star piece reaction only in isNrvEnableStarPieceAttack.
  Test `bsb` (HoneyBeeKingdomGalaxy/3, XF=-DPACKS_OVERLAY=wt-boss2): started, a hip drop in each phase, its star taken.
  ponytail: leg IK, BAS sounds, fly SE volume / IIR, the wing's BTK / BVA, BaseMatrixFollowTarget, shadow; StinkBugParent
  not ported (left in the class_make list). Repack: HoneyBeeKingdomGalaxy (slice_all.py + its sound.bin).
- Star return demos' BGM (#204, 2026-11-05): GrandStarReturnDemoStarter exeMove STM_FIRST_ASTRO / STM_SECOND_ASTRO,
  exeWaitDemoEnd MBGM_GALAXY_24 / STM_ASTRO_OUT*; StarReturnDemoStarter's AstroGalaxy overlayWithPreviousScreen(3) and
  endStartPosCamera. Seen with `grand` (HeavensDoorGalaxy/1 + the AstroGalaxy overlay). No repack.
- StrayTico's LauncherCamera (#206, 2026-11-05): exeLauncherCamera 400 behind Mario along the camera Z while a launch
  star flies him, height 150, friction 0.97. `stray` (FactoryGalaxy/2) still brings the CollectTico star. No repack.
- TicoFat leftovers (feat/ticofat, 2026-11-05): デブチコカメラ (camera_prog_start at its place, 100 up, 1300 .. 1680 in
  front by scale, 120 frames; again per piece eaten; ends at full), guidance / pointing / meter only after startEat,
  not full, not invincible; arg7 the StarPieceAlmsStorage slot (gamedata.c gd_alms / gd_alms_add: fed before -> dead
  at init with SW_A on, After / Meta add arg1), 喜び camera for arg7 6, Meta now turns SW_A on; TicoFatGoodsStarPiece
  goods at StarPieceL / R (slice.py EXTRA / SKINNED_OBJ; ..Far beyond 2000); reactions through npc_react (Kinopio's
  NPCActor code made shared): SE_SV_TICOFAT_TRAMPLED / POINT, SE_SM_TICOFAT_SPIN. `fat` (EggStar 2) still eats 100,
  flies, its planet. ponytail: goods' BPK colours, Reaction clips, _DB voice, isPlayerFlying. Repack: slice_all.py +
  sound.py for EggStar, BattleShip, Factory, HellProminence, OceanRing, SandClock, StarDust.
- TicoShop (feat/ticoshop, 2026-11-05): talkcls 11 on the TicoComet code (tcm: need arg0 / 30, gifts every 100 /
  need), Common_TicoShop000 (msg.py COMMON), events 3 / 4 the choice, its kill node the Demo nerve (Demo, META_ITEM /
  META, SMOKE, the chosen 1-up / life up given at once), TicoShopGoodsOneUp / LifeUp goods at StarPieceR / L (Shake);
  TicoComet / TicoShop reaction voices (npc_react). Test: `talk` XF=-DTALK_CLS=11 (KoopaJrShipLv1Galaxy/1, 50 pieces):
  30 eaten, Comprar, the demo, life up. ponytail: no popping mushrooms, Eat0..2 growth. Repack: slice_all.py + sound.py
  for KoopaJrShipLv1Galaxy, ElectricUfo / HellLavaFloating / PhantomNest galaxies (their TicoShop zones).
- Rosetta's BGM events (feat/6-rosetta-bgm, 2026-11-05, closes #6): eventFunc 0 / 2 unless BGM_FLYING_A plays:
  setNextStageBGM BGM_SENARIO_SEL_3 + stopStageBGM(30) / STM_ASTRO_OUT_3 + stopStageBGM(60); game.c bgm_next
  (AudBgmMgr::mNextBGM, started by game_bgm_tick once the stage stream stops). Test: `talk` AstroGalaxy/1 XF="-DTALK_CLS=9
  -DROS_BGM_TEST" (events 0 at f 600, 2 at f 1500: SEQ_senario_sel_3, SMG_astroout03_strm after the fades). The
  StarPieceCounter's Flash / Wait stay engine gaps (no runtime per-pane brlan in lyt.c; noted at spc_*). No repack.
- Static classes, first half (statics agent, 2026-11-06): engine/src/statics.inc (#included by game.c, st_*: st_update
  after update_gaps, st_draw in the world pass) ports the placed classes class_coverage listed as static.
  Flag (#329): Flag.cpp's cloth (SwingRopePoint rows, wind, random gusts, light colours, SE_OJ_FLAG, the stick) for
  Flag / FlagKoopaA / B / FlagRaceA / FlagKoopaCastle / FlagPeachCastleA / B / C / FlagTamakoro; slice.py TEX_ONLY
  gives their archives' lone .bti as an empty-group model (its texture); render.c render_mesh_vc (a colour a vertex,
  blend 3: blended with depth writes). Test: plain build XF="-DST_FLAG_TEST=k -DPACKS_OVERLAY=wt-states" (the camera
  on flag k): PeachCastleGardenGalaxy/1, KoopaJrShipLv1Galaxy/1. ponytail: FlagSurfing (OceanRingPipe's flags), the
  stick's texel. tools/class_coverage.py now reads game.c's .inc files too.
