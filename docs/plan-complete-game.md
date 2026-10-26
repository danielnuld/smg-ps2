# Complete game plan (Daniel, 2026-10-24)

Order: 1 animations + power-ups, 2 level objects with behaviour, 3 bosses, 4 test levels and engine demos.
Rules: every class ported literally from vendor/Petari (nerves, constants, clip names); no improvised behaviour and
no simplifications. Whatever does not fit (EE memory, VRAM, packs) is listed here and checked with Daniel by hand.
No interim ISOs: Daniel tests at the end.

## Phase 1: animations and power-ups

### Animations
- [x] 104 placed classes had a Wait clip and no own-named clip, drawn in their bind pose (Kinopio, Penguin, HoneyBee,
      TakoHei...): slice.py WAIT_SKINNED / WAIT_ARC (out/wait_classes.json) makes them skinned; obj_anim plays Wait.
- [ ] Check after the repack that every scenario still fits PACK_ROOM, and a few in PCSX2 (garden Toads, EggStar
      TakoHei, Gateway Lumas).
- [ ] Per class, the actions its Petari nerves start (startAction) instead of Wait only: done class by class in
      phase 2 with each behaviour.

### Power-ups (Petari MarioActorMorph setPlayerMode: 1 invincible, 2 fire, 3 ice, 4 bee, 5 rabbit / spring
### (Hopper), 6 Teresa / Boo, 7 Foo / flying)
- [x] Models per scenario (slice.py MORPH_ARCS / morph_arcs = makeArchiveList: HopperMario, BeeMario + BeeMarioWing,
      TeresaMario, IceMario, InvincibleMario and the Luigi ones; CrystalBox / CrystalBoxBreak / MorphBubble by arg3);
      MORPH_SKIN skinned on Mario's 30 joints (game.c m_morph / morph_anim, hideJoint Face0). Fit checked on 4 galaxies.
- [x] Fire / Foo ColorChange BTP: SWAP kind 1 on Mario's Body1_v / Cap_v (MarioBody / MarioCap .0 .1 .2).
- [x] MorphItemObjNeo (game.c mi_*: Wait / Wait2 / SwitchAppear / Appear / Fly, box and bubble, appear cameras and the
      出現 demo, glow / appear effects, revive on mode end / replace, invincible knock away).
- [x] setPlayerMode / updatePlayerMode / resetPlayerModeOn(No)Damage / touchWater / updateMorphResetTimer, isActionOk
      and selectAutoBind tables (launch stars, poles, pipes, trapeze, branches / ropes, water bullets, GCapture), the
      マリオ変身 / マリオスーパー化 programmable demos (pd_*: the scene stops), the flicker, 変身初出カメラ
      (camera_prog_*), InformationObserver's first-time window (lyt.py MSG_PREFIXES InformationObserver).
- [x] Spin by mode (updateSwingAction / selectAction スピンアタック: 1 spin, 2 fire ball, 3 freeze, 4 Teresa, 0 none).
- [x] Fire: shootFireBall (hand, anims FireSpin / Throw, hop, _42C) and FireMarioBall exeThrow / tryToKill line by line.
- [x] Bee: beeMarioOnAir / beeMarioOnGround / incAirWalkTimer (coins too), bee jump / spin / hip drop clips, gravity
      revive (_76C), drop limit, Karikari scale, wings (BeeMarioWing Wait / Fly / FlyWait / FlyFall at Spine2), hands 0.9.
- [x] Bee wall walk on Fur walls / ceilings / floors (updateBeeModeGravity, checkBee*Stick, updateBeeStickMode,
      entryWallWalkMode; creep clips, creep landing). BeeWallShortDistArea / the ハチ出立 host: placed nowhere.
- [x] MarioSubMeter (menu.c subm_*: FlyMeter / AirMeter, Appear / Wait / Danger / End, over Mario).
- [x] Invincible: blend kind 4 (frame buffer x mean(C0) + C0, its C0 the BRK track: slice.py brk_c0 / BRKC, game.c
  brkc_apply, render.c render_group_tint) and its MutekiKankyo2 env layer; the hands follow the body's frame.
- [x] Ice: MarioSkate, doFreezeAttack, IceStep floors (collision) on water, no swim, no DamageFire, no smoke.
      (TransparentWall's ice walls: placed nowhere.) [ ] the freeze attack's hits on enemies (phase 2 reactions).
- [x] Spring: MarioRabbit (bounce, charge, turn, lean of Hip / Spine1), forceJump, hip drop clips.
- [x] Boo: MarioTeresa (float, A push, wind from BigFans, reflections, vanish through GhostThroughCode walls),
      TeresaMario (基本 wait / run blend: anim_blend, fly / fall / spin / hit / change, BTP eyes, Erase alpha).
      GlaringLightArea / SunLightArea: placed nowhere. [ ] heat beam, Teresa sensor messages per enemy (phase 2).
- [x] Flying: MarioFoo line by line (jet, tilt, spin, brake beam and trail with FooLine.bti as added strips,
  checkBaseTransBall, CelestrialSphere, passRing for DashRing in phase 2); tryStartFoo from the spin jump.
- [x] MarioFireBall: skinned, its Roll BCK, mRotation from the throw, initShadowVolumeCylinder(25); in common.bin.
- [x] Hands: MarioHandL / R with the player's HandL0 / HandR0 material (slice.py face_bdl), Ice / Invincible ones,
  pose by PartsControl y / z; HandL0 / R0 hidden on his models (not Hopper).
- [x] Damage statuses (MarioDamage / Faint / Flip / Blown / FireDance / FireRun / Paralyze with BoneMario /
  Freeze with MarioFreezeIce / Crush / Stun) and receiveMsgEnemyAttack (game.c mario_attack, ATK_*): the objects
  still send the plain attack (hurt_mario) until each is ported with its own message in phase 2.

- [x] MarioFace (initFace / updateFace: 8 faces by PartsControl, Mario's Face0 material: slice.py face_bdl).
- [x] Runtime BTP texture patterns (slice.py btp_tables / BTPS, SWAP kind 0x80 | slot; game.c btp_apply in draw_skin).
- [x] FallsCube (touchWater, 水壁ヒット). Two-track blend in anim.c (anim_blend).

- [x] Blend groups (XanimeBckTable2-4: 基本, 水泳基本, しゃがみ基本, その場足踏み...: anim.c anim_group, 4 tracks at
  one phase, XanimeCore::calcBlend), MarioAnimator weights (setBlendWeight / setWalkWeight), controlWaitAnimation
  (slope waits), changeTrackAnimation (DamageWait); the upper body layer (XanimePlayerUpper: anim_overlay).
- [x] AreaFormCube: center-origin cubes (createCenterOriginCube) told from base-origin ones (area_centered).
- [x] marioAnimeTable generated whole (tools/mario_anims.py -> mario_anims.h: 307 names, clip, rate, interpolation).
- [x] New blend kinds: 4 (Cd x FIX + Cs), 5 (Cd x (1 + FIX)) for the screen-copy materials (InvincibleMario,
  MarioFreezeIce's HiLightMat); additive strips (Strip.pad bit 0).

### Engine gaps found on the way (to do in this phase unless they belong to a later one)
- [x] Runtime BVA (slice.py bvas_entries / BVAS, game.c bva_apply in draw_skin, VuGroup.hide).
- [x] Sequenced BGMs (JaiSeq): tools/bgmseq.py renders the 45 with no stream (power-ups, bubble, jingles, scenario
  select, Library, dome loft) into out/music/SEQ_*.pcm through sound.py's sequencer; music.py maps them (bgm_tab /
  music_tab). bgm_sub = AudBgmMgr's sub BGM on the one stream: the stage's place kept, back when the sub ends with
  faderInterruptedRecoverTime 60 (audio_seek). [ ] by ear: their loudness against the streams (one gain 0.326 for
  all of them, from the loudest peak; the absolute JAudio mix level is not in the decomp).
- [x] MarioSwim line by line (game.c sw_*: checkWaterCube, buoyancy, waves, strokes, spin dash, Z sink, the 水泳基本
  group, tilt, oxygen / cold water, damage / faint in water, ring dash, exits) and MR::getWaterAreaInfo (water_obj /
  water_info: WaterCube / Sphere / Cylinder, OceanSphere, OceanBowl). Pending with their objects: OceanRing water, the
  JetTurtle shell (jet). MarineSnow (draw3D) done: render_sprites with MarineSnow.bti (fx_marinesnow_tex).
- [ ] Invincible afterimages (MarioActor::drawModelBlur, _A6E / calcViewBlurModel): the 7 past poses every 3 frames
  are drawn with the _94 display lists, which the decomp never builds (their material is unknown): check by hand
  against the Wii what they look like before porting; 7 extra skinned Marios a frame is also its cost on the GS.
- [x] Slippery floor (_35 = isSkatableFloor: Ice / GlassIce) in mainMove: decideInertiaOnSlip, ブレーキ滑り床 (setWalkMode),
  ターンブレーキ滑り床 + taskOnSlipTurn (_2B8 x mInertiaSlipTurn, mTurnSlipTimeB), no brake timer on it (MarioWalk);
  createCorrectionMtx (_74C eased over _750 / to pi over _754; setFrontVecKeepUp(front, n)): the standing turn no
  longer snaps; MarioAnimator only replaces walk / wait motions (the run-clip loops stay). ICE_TEST (every floor
  skatable) checked it in PCSX2. 雪煙レベル (registerEffect CrestGetFlyC at FootR: jpa.py REGISTERED) on it.
  updateEffect's offPlayerControl (MarioActor::_3C0) was read as Mario's _3C0 (drop wait): fixed (demo_on).
  [ ] by hand: right after the slip turn, with the stick still held back, the standing turn re-eases _74C from pi
  (as decompiled: setFrontVecKeepUp(f, n) keeps _74C when the drawn front already matches) - check on the Wii.
  Not ported: _34 (ice inertia / decideOnIceAnimation: no setter in the decomp); 宇宙遊泳 (needs _240 near zero,
  which updateGravityVec never leaves); ジャンプフェアリー (only deleted).
- [x] Multi BGM states (setStageBGMState): Gateway's rabbit chase goes 2 / 3 / 2 / 1 in PCSX2 (mbgm.py: 62 tracks).
- [x] 飛び込み準備 (decideWalkAnimation: MarioSwim::_1B2 from updateUnderWater, deep water over 300 ahead; cleared by
  the map there): ice_under_water now sets / clears it; the swim test runs as before.
- [ ] PCSX2: the swim test (Beach Bowl) stalls ~21 s of host time between its frames 780 and 800 underwater, the
  same with the build from before these changes (not new); check on the console whether it is the emulator.
- [~] MarioHang (paused 2026-10-25): KCL triangles (Hit pos / prism / flag, Tri, map_poly exact line, prev part
  matrices), Mario's map info (checkAllWall front / side / back, calcFrontFloor, calcShadowPos, recordLastGround),
  checkHang front / back / side, MarioHang start / update / close / tryClimb / postureCtrl, tryHangSlipUp,
  setBlendMtxTimer (blendMtx from the last drawn matrix). Also: createCorrectionMtx's turn is now drawn (it was not).
  PCSX2 HANG_TEST: catch + hangwait verified. [ ] verify climb (test fixed to push along his front, not rebuilt),
  stick-back drop, slip up; then remove the HANG_TEST printf's in m_check_hang / hang_update / hang_try_climb.
  Not kept (by hand): slope-ahead data (_4DC/_4D8/_380), checkStrikePointToMap, Teresa's thick ball, hang camera,
  animated parts (leaves / Move joint) always count as moving. Pending with it: tryDrop's wall branches. SlipUp
  effect added to jpa.py (repack).
- [ ] Mario statuses not ported (inventory 2026-10-25, in play order): MarioHang (cliff hang / climb), MarioSideStep,
  updateOnSand / updateOnPoison, MarioBump / MarioStep / MarioFrontStep, MarioMove2D / 25D / Mario2D, MarioSlider,
  checkSidePress, MarioRecovery / doPointWarp / doCubeWarp, MarioStick, MarioFlow, MarioSpin (tornado tilt, wall),
  MarioSukekiyo, MarioActorBlackHole, bodyClap, PlayerEvent FireDown / RaceDown / GhostRaceDown + drawPreWipe,
  MarioSearchLight. Done: Faint, Blown (but its checkWallJumpHit case), the damages, SwimDamage, Eye, FpView.
- [ ] TreasureBox's KinokoLifeUp gives it at once (its BenefitItemLifeUp should pop out): phase 2.
- [ ] FountainBig's bee flip (phase 2 object); BeeFlowerHover (phase 2); MarioStick (no decompiled class sends
      ATYPE_BEE_FLOWER: nothing sticks).

## Phase 2: level objects (class_coverage.md: static 222 classes, missing 36)
- [x] Gateway rabbits (Daniel: "found one, it did not respond"): the opening plays through as a player would
  (GATEWAY_TEST with XF=-DGW_FOLLOW: Mario walks after the bunny, into the bushes / pipes, onto the rabbits; 3 of 3
  caught, the tower demo starts). Fixed on the way, all against Petari: DemoRabbit::updateJump jumps whenever bound
  to a wall (calcHitPowerToWall is never below 0; the port wanted velocity into it, so the bunny stalled and an
  invented 300 hop got it through: removed); updateNormalVelocity / StopVelocity with addVelocityToGravityOrGround
  and reboundVelocityFromCollision; RunawayRabbit's SW_APPEAR listener fires in any nerve (startRunnaway).
- [x] Exact segment collision: kcl.c kcl_check_arrow (KCollisionServer::checkArrow + KCHitArrow, a start on a face
  does not hit) and game.c kcl_line over every collision (kcl_each, shared with kcl_query); line_hits
  (isExistMapCollision) uses it. [ ] map_line (getFirstPolyOnLineToMap) still steps spheres: move it too, then check
  Mario's ceiling / press / bee wall callers.
- [x] Mario's areas (MR::getAreaObj: game.c area_obj; slice.py LOGIC keeps them): NonSleepCube (MarioWait:
  戦闘ウエイト; Teresa's 1B at 210), TowerModeCylinder (Mario round the tower: stick y off, move dir along it,
  fixPositionInTower, fixFrontVecByGravity), MessageAreaCube / Cylinder (TalkMessageCtrl::inMessageArea), EffectCylinder
  and SmokeEffectColorAreaCube (doCubeEffect). CelestrialSphere was not in the packs (Flying Mario's foo_sphere): now.
  [ ] check the tower by hand (Good Egg 4's ReverseGravityTower: the test spot met a wall).
  [ ] SpinGuidanceCube / BlueStarGuidanceCube show Wii remote / pointer guidance layouts: decide with Daniel.
  [ ] AudioEffectCube (AudEffector: echo / reverb), ViewGroupCtrlCube: to do.
- [x] Multi BGMs (AudMultiBgm): every MBGM_ is a stream (pairs A / B) and its sequence in sync, with mute states
  (AudBgmSetting cMuteState): tools/mbgm.py mixes each state offline (sequence warped onto the stream's beats, the
  second loop mixed in), out/music/<stream>[@k].pcm + engine/src/mbgm_tab.h; game.c bgm_state (setStageBGMState) swaps
  file at the same place; RunawayRabbitCollect's states 2 / 3 / 1. Run tools/music.py before tools/mbgm.py (it
  writes the same stems). [ ] the other setStageBGMState users (ChangeBgmCube, bosses, SphereSelector, FileSelector,
  TeresaRacer, SpringJetWater, BgmProhibitArea, AudBgmConductor) with their classes. [ ] by ear: the sequence /
  stream balance (items' volumes / 255) and the 4-frame dip at a state change.
- [x] MarioEffect: cSmokeTable whole (20 entries, 7 materials x 4 variants, intervals, joints, wall forms, water
  only), getFloorMaterialIndex, cMaterialEffectTable (hip drop / bee wind / spin / stage-in by material),
  updateOnWater's water floors (20..23), fx_tint (setGlobalPrmColor, setRate). Call sites: 壁手擦り (play / stop),
  スリップ坂制動, 跳躍 (water only), 雪煙レベル done; 水波紋 + 水はね(弱) on each wading step (decide(Squat)Walk
  Animation's tail, mIsUnderwater also from checkWaterCube with _738 / _73C), WADE_TEST in PCSX2 (steps at the
  walk's frame 0 / 30). No code plays スリップ空転 / 共通水底接触 / ひこうき雲 / 壁上昇 (only stopped) / 宇宙遊泳
  (see above). [ ] ブラックホール / 引き戻し着地 / 着地大 with their statuses (black hole fall, pull-back,
  MarioLauncher / SpinCannon endRush). [ ] by hand in Beach Bowl: the wading splash / ring sizes (0.2 scale).
## Phase 3: bosses
## Phase 4: tests (levels in order, engine demos). Known: the Gateway RunawayRabbit did not respond to Daniel.
