# Where phase 2 stands (resume notes)

Last updated 2026-10-09. Detail and numbers: `docs/phase2-report.md`; tasks: `openspec/changes/phase-2-full-engine/tasks.md`.

## Rules

- Fidelity first (user requirement, 2026-10-11): behaviour is ported exactly as Petari (vendor/Petari) has it -
  constants, reference frames, joints and hit sensors, nerves and their order, timings, clip names. No values
  guessed from data and no approximate versions where the decompiled code exists; code written before that way is
  rewritten (Good Egg first). Only where Petari lacks the code (undecompiled functions, commented-out constants,
  services the PS2 port does not have) is a gap filled, marked `ponytail:` naming it. 30 fps is acceptable.

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
    X jumps off; no FluffWind); a rebind keeps what Mario rides (by its row);
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

## How to run

| What | Command (from Windows, `MSYS_NO_PATHCONV=1` in Git Bash) |
|---|---|
| Convert one scenario (embedded blob) | `python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice/hd2.bin` |
| Convert everything (packs + table, ~5 min) | `python tools/slice_all.py data/fs/files out/packs 6` then `python tools/vram_need.py out/packs` |
| Build, embedded Gateway 2 | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench]` → `engine/slice[-bench].elf` |
| Build from packs | `wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh MODE out/slice/hd2.bin Galaxy/n` → `out/slice-packs[-MODE].elf` (run from `out/`: `host:packs/`) |
| Modes | `plain`, `bench`, `slope`, `zone` (first SuperSpinDriver at frame 120), `box` (FlipPanelEx 1: the coin pops and is taken), `collapse`, `hazard` (EggStar 1: third NeedlePlant prick is a miss; BattleShip 2: mine, electric rail, black hole; Factory 1: FireBar), `item` (BattleShip 2; CosmosGarden 1: a QuestionCoin's dead switch brings star pieces), `spin` (BattleShip 2, CannonFleet 1), `pipe` (BattleShip 2), `hip` (Factory 1), `flip` (FlipPanelEx 1: 18 panels, the group completes, its power star comes out and is taken), `cage` (CosmosGarden 3, SandClock 1: two spins at the CrystalCageL, then its star), `restart` (BattleShip 2: a miss after each of two RestartCubes), `spot` (BattleShip 1: cages whose dead switch bursts a StarPieceSpot), `cannon` (CocoonEx 1: Mario in a launcher's line of fire), `shoot` (StarDust 1: ShootingStar bursts), `plant` (EggStar 1: Mario through every member of the plant groups with items), `steam` (IceVolcano 2: Mario in a LavaSteam vent), `geyser` (IceVolcano 2: Mario in a LavaGeyser column), `assembly` (StarDust 1: Mario over 20 AssemblyBlock places), `jumpstand` (Factory 1: dropped on a JumpStand, then in a hip drop), `warppod` (HeavensDoor 2: into a WarpPod twice), `tsuki` (IceVolcano 1: Mario on a Tsukidashikun), `kiraira` (OceanPhantomCave 1: Mario into a Kiraira every second), `takobo` (EggStar 1: a Takobo punches, a stomp, a spin), `meteor` (FloaterOtaKing 1: Mario under a MeteorStrike; the test shows them without their boss switch; `TEST_SHOWN` in game.c), `killer` (BattleShip 1: Mario 1500 in front of a HomingKiller), `mkoopa` (Factory 1: a MechanicKoopaMini fires, a stomp, a hip drop), `karikari` (EggStar 1: Karikari cling, bite, a hip drop throws them off), `flow` (CocoonEx 1: Mario takes moving StarPieceFlow pieces), `cursor` (CocoonEx 1: the pointer sweeps the screen taking star pieces), `gcap` (StarDust 1: R2 on a GCaptureTarget pulls Mario, then he floats and falls), `shoot2` (EggStar 1: a star piece shot stuns a Takobo, a stomp), `tspot` (BattleShip 2: shots open TreasureSpots), `kmini` (BattleShip 1: KuriboMini stomped, spun), `karon` (SandClock 1: a Karon broken and back), `prom` (HellProminence 1: Mario on a LavaProminence's rail), `stone` (CubeBubbleExLv1 1: spins break SmallStone rings), `teresa` (Phantom 1), `beeflower` (HoneyBeeKingdom 1), `butterfly` (HeavensDoor 2), `ticorail` (Astro 1), `jbranch` (OceanPhantomCave 1), `giver` (ReverseKingdom 1 / HoneyBeeKingdom 1), `biri` (BattleShip 2), `punch` (EggStar 1), `coco` (OceanPhantomCave 1), `cocoon` (CocoonEx 1), `fluff` (CosmosGarden 1), `waterplant` (OceanRing 1), `stink` (KoopaJrShipLv1 1), `unizo` (OceanRing 1), `note` (EggStar 1), `key` (Phantom 1), `poihana` (HeavenlyBeach 1), `jet` (OceanRing 1), `screw` (Factory 1), `candle` (Phantom 1), `fountain` (HoneyBeeKingdom 1), `fire` (OceanPhantomCave 1), `bee` (HoneyBeeKingdom 1), `stray` (Factory 2), `swim` (HeavenlyBeach 1 / FishTunnel 1: 300 under a water surface, floats up and stays), `bubble` (HeavenlyBeach 1: Mario over an AirBubbleGenerator), `dino` (EggStar 1: the tail ball punched four times, the star taken), `fat` (EggStar 2: Mario before the TicoFat pointing with L1 held: 100 gifts, its fly, planet D and the pink SuperSpinDriver), `ota` (EggStar 3: Mario on its bath's rim stepping into returned coconuts, rallies of 1 / 2 / 3, the star taken), `purple` (EggStar 5: 100 purple coins, then the 100-coin star), `walker` (EggStar 1: Mario by a Kuribo, Takobo, Karikari, Butterfly), `route` (EggStar 1-4 played through from the start: launch stars toward the goal; a hidden one's switch writer (chip group, cage, CocoSambo) done first, walked to within its zone; then the boss / TicoFat and the star: all four get their star); `killer` also freezes a bullet with R2 |
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
