# Class audit A: MapObj/ A-M (2026-11-18)

Each placed class whose Petari source is `src/Game/MapObj/[A-M]*.cpp`, most placed first, compared with the port:
nerves and what they start, sensors and messages, switches, sounds, effects. Verdicts: ok (what differs is marked
`ponytail:` in the port, named here), fixed in a PR, or an issue with the list of what is missing. The comparison was
read from the sources (Petari file:line against game.c / statics.inc / the other .inc); nothing here was run in PCSX2.

| Petari class | placed | placed names | verdict |
|---|---|---|---|
| InvisiblePolygonObj | 555 | InvisibleWall10x10:463, InvisibleWall10x20:90, InvisibleWallJump10x20:1, GhostShipCavePipeCollision:1 | ok (collision only; GhostShipCavePipeCollision is named nowhere: #653) |
| GCaptureTarget | 537 | GCaptureTarget:537 | issue #647 (no effects, no clips, SW_B, the appear nerve, the pointed level SE) |
| CrystalCage | 522 | CrystalCageS:484, CrystalCageM:30, CrystalCageL:8 | issue #648 (the item inside never drawn, SW_DEAD's delay, enemy attacks, arg0 as a bool, the stump's place, arg2's binder) |
| CollapsePlane | 473 | CollapsePlane:473 | ok |
| AssemblyBlock | 343 | AssemblyBlockPartsA:122, AssemblyBlockPartsB:74, AssemblyBlockPartsTimerA:55, AssemblyBlockPartsE:32 ... | ok (marked ponytail: collision off in flight, Disappear's BRK, blendMtx) |
| BenefitItemOneUp | 265 | BenefitItemOneUp:265 | fixed in PR #(#645's) (the placed one's Get effect and SE_SY_1UP); the rest issue #652 |
| CollisionBlocker | 225 | CollisionBlocker:225 | ok |
| ItemBlock | 195 | TimerPieceBlock:98, SplashPieceBlock:74, SplashCoinBlock:12, TimerCoinBlock:11 | ok (marked ponytail: Glow; not checked: the CoinBlock BRK, Standby's Wait clip) |
| FlipPanel | 181 | FlipPanel:165, FlipPanelReverse:16 | issue #653 (the flip on jumping off, the observer's SW_APPEAR) |
| LavaSteam | 151 | LavaSteam:151 | fixed in PR #(#645's) (SW_A only starts it; the Sign effect's shrink) |
| BeeFlowerHover | 145 | BeeFlowerHover:145 | ok (marked ponytail: its rail, #653) |
| IronCannonLauncherPoint | 142 | IronCannonLauncherPoint:142 | ok |
| HipDropRock | 142 | HipDropRock:142 | ok (marked ponytail: break model, wreck, SW_DEAD at step 30: #648) |
| ElectricRail | 139 | ElectricRail:139 | issue #653 (the demo's Disappear and its SE, the hum's place; the shrink marked ponytail) |
| AstroMapObj | 120 | AstroDomeEntrance:30, AstroStarPlate:30, AstroDecoratePartsGearA:10, AstroBaseA:5 ... | issue #650 (Dead / Alive / Revival, Open, KitchenSmoke, name plates; marked ponytail) |
| InvisiblePolygonObjGCapture | 111 | InvisibleWallGCapture10x20:111 | issue #647 (ACTMES_IS_NO_BREAK_G_CAPTURE: the pull star lets go on it) |
| Candlestand | 106 | PhantomCandlestand:42, TeresaMansionCandlestand:40, CandlestandIceVolcano:24 | fixed in PR #(#645's) (all its sounds, the Attack nerve, the burn reach, Flicker / Extinguish02, offSwitchA, the item's place and coin angle); lit by an enemy's fire left |
| BreakableCage | 95 | BreakableTrash:45, BreakableCage:41, BreakableCageRotate:6, BreakableFixation:2 ... | issue #648 (BreakableFixation not ported; what breaks a cage, its SE / break model / scene stop, Rotate's turn, the camera demo) |
| MeteorStrikeLauncher | 92 | MeteorStrike:55, MeteorStrikeEnvironment:22, MeteorCannon:15 | issue #653 (the launch cadence, the MeteorStrike / Break effects) |
| CoconutTreeLeafGroup | 90 | CoconutTreeLeaf:90 | ok (the first wind's step: #653) |
| FireBar | 76 | FireBar:76 | ok (marked ponytail: the balls' BTK) |
| AnmModelSwitchMove | 75 | ShutterDoorC:10, MechaKoopaPartsCaterpillar:6, HeavenlyBeachUnderRock:6, LavaFallDownColumnA:6 ... | ok (a demo cast's start not checked: #653) |
| BlueChip | 73 | BlueChip:73 | ok (sCannotGetTime, arg3's bubble, a rail: #653) |
| LavaGeyser | 73 | LavaGeyser:73 | ok (marked ponytail: its clips) |
| JetTurtle | 70 | JetTurtle:70 | issue #649 (homing, rebound walls, effects, clips, the waiting shell's messages) |
| LavaStrangeRock | 69 | LavaStrangeRock:40, LavaStrangeRockL:29 | fixed in PR #(#645's) (the break SEs' fall-through, the hit point) |
| CocoNut | 67 | CocoNut:67 | issue #649 (WaitOnBind, reviseFrontVec, trample / invincible flips, stopScene) |
| BrightSun | 58 | BrightSun:58 | ok |
| BenefitItemLifeUp | 58 | BenefitItemLifeUp:58 | fixed in PR #(#645's) (the Get effect); the dark comet: #652 |
| JumpStand | 56 | JumpStand:56 | ok (stopSound SE_PM_HIPDROP: no stop by id in se.c, #653; marked ponytail: the bind matrix, collision off) |
| LavaProminence | 55 | LavaProminence:43, LavaProminenceWithoutShadow:12 | ok (marked ponytail: effects, the ball's turn) |
| AirBubbleGenerator | 46 | AirBubbleGenerator:46 | ok (marked ponytail: the Generate clip) |
| DeadLeaves | 42 | DeadLeaves:42 | fixed in PR #(#645's) (the Spin clip, SE_OJ_LEAVES_SWING, SE_OJ_STAR_PIECE_BURST, every spin) |
| CoinReplica | 39 | CoinReplica:39 | ok (marked ponytail: arg0) |
| KeySwitch | 36 | KeySwitch:36 | fixed in PR #(#645's) (the placed key's get SEs and effect); its appear nerve and Rotation: #653 |
| FountainBig | 34 | FountainBig:34 | ok |
| AnmModelGroundOnMove | 34 | LavaBreakRockD:16, LavaBreakRockB:6, LavaBreakRockC:6, LavaBreakBridge:6 | ok |
| AirBubble | 32 | AirBubble:32 | ok |
| BlueStarCupsulePlanet | 30 | BlueStarCupsulePlanet:30 | ok (marked ponytail: effects, React: #647) |
| MiniatureGalaxy | 29 | MiniTriLegLv1Galaxy:1, MiniHoneyBeeKingdomGalaxy:1, MiniEggStarGalaxy:1, MiniFlipPanelExGalaxy:1 ... | fixed in PR #(#645's) (SE_SY_KOOPA_LAUGH); effects, BRKs, plates: #650 |
| BenefitItemInvincible | 27 | BenefitItemInvincible:27 | issue #652 (its own bouncing Escape, appearThrowUp, the dark comet, sensor 60) |
| YellowChipGroup | 26 | YellowChipGroup:26 | ok (marked ponytail: arg1's flashing chips, set by no group on the disc) |
| FallingSmallRock | 26 | FallingSmallRock:26 | ok |
| ArrowSwitchTarget | 24 | ArrowSwitchTarget:24 | ok |
| AnmModelBindMove | 21 | TeresaRacePartsB:21 | issue #647 (TeresaRacePartsB never starts Move) |
| BlueChipGroup | 20 | BlueChipGroup:20 | ok |
| BigFan | 19 | BigFan:12, BigFanNoLeg:7 | ok (marked ponytail: the Stop nerve) |
| FirePressureRadiate | 18 | FirePressureRadiate:18 | ok (the cannon sensor's push: #653) |
| ItemBubble | 18 | ItemBubble:18 | ok |
| LavaGalaxyParts | 17 | LavaShellTowerStep:6, LavaStepB:4, LavaStepA:4, LavaStepC:3 | ok |
| LotusLeaf | 16 | LotusLeaf:16 | ok |
| ElectricRailMoving | 14 | ElectricRailMoving:14 | ok (its points' push after the vanish: #653; drawing marked ponytail) |
| ChooChooTrain | 14 | ChooChooTrain:14 | ok (marked ponytail: the turn rate; the cars' Run clip not checked) |
| DesertMovingLand | 14 | DesertMovingLand:13, DesertMovingLandSmall:1 | fixed in PR #(#645's) (DesertMovingLandSmall run, SE_OJ_LV_DESERT_LAND_SIGN); marked ponytail: the press stop, the demo's hold |
| DesertLandMoveSwitch | 14 | DesertLandDownSwitch:9, DesertLandUpSwitch:5 | fixed in PR #(#645's) (the timer SE's guard); MapObjConnector: #653 |
| ElectricBall | 13 | ElectricBall:13 | issue #653 (its push) |
| FlipPanelObserver | 13 | FlipPanelObserver:13 | issue #653 (the group dead until its SW_APPEAR) |
| ArrowSwitch | 12 | ArrowSwitch:12 | issue #653 (the lock's sensors / collision, its push; BTK marked ponytail) |
| LavaHomeVolcanoFlow | 12 | LavaHomeVolcanoFlow:12 | ok |
| LavaBreakColumn | 12 | LavaBreakColumn:12 | issue #653 (SE_OJ_LAVA_COLUMN_FALL_ED, the LavaColumn effect) |
| AnmModelSwitchMoveEndKill | 12 | HeavenlyBeachTopRock:6, LavaBreakRockA:6 | ok |
| HipDropSwitch | 9 | HipDropSwitch:9 | ok (MapObjConnector: #653; marked ponytail: BRK, spring) |
| ClipFieldSwitch | 8 | ClipFieldSwitch:8 | ok (the 5.0 margin is not used by ClipAreaShape::isInArea) |
| DashRing | 7 | DashRing:7 | ok (marked ponytail: BRKs) |
| FlexibleSphere | 7 | FlexibleSandPlanetPartsA:7 | ok (marked ponytail) |
| HeavensDoorDemoObj | 7 | HeavensDoorInsideCage:5, HeavensDoorInsidePlanetPartsA:1, HeavensDoorAppearStepA:1 | fixed in PR #(#645's) (Vanish's stage effect SEs); Move's clip marked ponytail |
| AstroDomeAsteroid | 6 | AstroDomeAsteroid:6 | ok |
| AstroDomeDemoAstroGalaxy | 6 | AstroDomeDemoAstroGalaxy:6 | ok |
| AstroDomeGalaxySelector | 6 | AstroDomeGalaxySelector:6 | issue #650 (SW_A, the comet's scenario select) |
| AstroDomeComet | 6 | AstroDomeComet:6 | ok (marked ponytail: BTK, bloom model: #650) |
| AstroDomeBlueStar | 6 | AstroDomeBlueStar:6 | issue #650 (the Appear nerve, the traction SEs, clips, ribbon) |
| AstroDomeSky | 6 | AstroDomeSky:6 | ok (marked ponytail: JumpOutDemo's rotation) |
| AstroDome | 6 | AstroDome:6 | issue #650 (not ported: a still model) |
| AnmModelSwitchMoveEventCamera | 6 | TeresaRoomDoor:6 | ok (marked ponytail: its event camera, #653) |
| ArrowSwitchMulti | 6 | ArrowSwitchMulti:6 | ok (marked ponytail: BTK, PlaneModeCube) |
| AstroCore | 5 | AstroCore:5 | ok (marked ponytail: ShockWave, BRKs, push: #650) |
| AstroOverlookObj | 5 | AstroOverlookObj:5 | ok |
| AstroMapBoard | 5 | AstroMapBoard:5 | ok |
| KillerGunnerSingle | 5 | KillerGunnerSingle:5 | ok (marked ponytail: the charge BTK) |
| GravityLightRoad | 5 | GravityLightRoad:5 | issue #651 (not ported: a still model) |
| IronCannonLauncher | 4 | IronCannonLauncher:4 | ok (the SW_A gate: #653; marked ponytail: the Shot clip) |
| HipDropTimerSwitch | 4 | HipDropTimerSwitch:4 | ok (MapObjConnector: #653; marked ponytail) |
| ClipAreaDropLaser | 4 | ClipAreaDropLaser:4 | ok |
| LargeChain | 4 | LargeChain:4 | fixed in PR #(#645's) (the fixed points' own Break and SE) |
| CoconutTree | 4 | CoconutTree:4 | ok |
| BallRail | 4 | BallRail:4 | issue #653 (its gravity each frame) |
| BallOpener | 3 | BallOpener:3 | ok |
| ManholeCover | 3 | KoopaShipManholeCover:3 | ok |
| CoinBox | 3 | CoinBox:3 | ok |
| JumpHole | 3 | JumpHole:3 | ok |
| CapsuleCage | 2 | CapsuleCage:2 | ok |
| LavaBallRisingPlanetLava | 2 | LavaBallRisingPlanetLava:2 | ok |
| CoinSpot | 2 | CoinSpot:2 | ok (every nerve waits) |
| CannonFortressBreakStep | 1 | CannonFortressBreakStepA:1 | ok |
| Fountain | 1 | Fountain:1 | ok |
| FirePressure | 1 | FirePressure:1 | ok (marked ponytail: the bullet's sensors, its BTK) |
| GoldenTurtle | 1 | GoldenTurtle:1 | issue #649 (the gold chest, as JetTurtle) |
| ItemAppearStone | 1 | ItemAppearStoneSpinDriver:1 | ok |
| MagicBell | 1 | MagicBell:1 | ok |
| FallDownBridge | 1 | FallDownBridge:1 | ok |
| ExterminationChecker | 3 | ExterminationKuriboKeySwitch:1, ExterminationCheckerKuribo:1, ExterminationSkeletalFishBaby:1 | ok |
| Coin / CoinGroup | 3144 | Coin:1362, PurpleCoin:1538, RailCoin:176, CircleCoinGroup:38 ... | issue #646 (Coin::exeMove, the popped coins: lava, death areas, water, wall rebounds, the binder, the flashing); the placed coins' nerves not audited |

## Skipped

Other agents' classes today: EarthenPipe (263), BlackHole (191), LavaProminenceTriple (21), MarioLauncher (10), HipDropMoveObj / HipDropDemoMoveObj (14), AstroCountDownPlate (5); CrystalCageMoving, RandomEffectObj, QuestionBoxGalleryObj and the rest of the brief's list are outside A-M's files or theirs too.
MorphItemNeo* (MorphItemObjNeo.cpp: done in phase 1 of docs/plan-complete-game.md) and the EffectObj* ambient effects (fx_tab.h) were not audited.
BigBubbleGenerator / BigBubbleMoveLimitter (BigBubble.cpp, ported in detail) were not audited.
