#!/bin/bash
# Build the slice ELF in WSL (the SD card through drvfs is slow, so build on the WSL disk).
# usage (from Windows): wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench|plain|slope] [blob, default out/slice/hd2.bin] [Galaxy/n]
# disc (4th argument): read the packs from the disc image (-DDISC), ELF to out/disc/SLICE.ELF for tools/disc.py
# Galaxy/n: boot from the streaming packs in out/packs (host:packs/) instead of the linked blob; the ELF is
#           also copied to out/slice-packs[-bench].elf, run it from there
# hazard: with packs, Mario walked into the first SpaceMine (frame 120), NeedlePlant (300, 400, 500: the third prick
#         is a miss), ElectricRail (600), black hole (700), FireBar (800) and a rolling rock (900)
#         (-DHAZARD_TEST)
# item: with packs, Mario put on each PowerStar / 1-up / life-up mushroom / QuestionCoin in turn from frame 120
#       (-DITEM_TEST)
# collapse: with packs, Mario dropped onto the first CollapsePlane at frame 120 (-DCOLLAPSE_TEST)
# box: with packs, Mario spinning on the first breakable box at frame 120 (-DBOX_TEST)
# spin: with packs, Mario caught by the first SpinDriver at frame 120 (-DSPIN_TEST)
# cover: with packs, a launch star at frame 60, one "cover:" line at frame 900 (tools/coverage.ps1, -DCOVER_TEST)
# enemy: with packs, Mario stomps the first TakoHei (120), spins by a PackunPetit (240), stands by one (360) (-DENEMY_TEST)
# overview: with packs, the camera orbits the whole galaxy (-DOVERVIEW_TEST; for pictures and videos)
# rail: with packs, Mario dropped onto the first rail-moving map part at frame 120 (-DRAIL_TEST)
# rotor: with packs, Mario dropped onto the first rotating map part at frame 120 (-DROTOR_TEST)
# restart: with packs, Mario put in RestartCube 0 (120) and 1 (300), a miss 80 frames later each (-DRESTART_TEST)
# spot: with packs, Mario spins at each breakable whose dead switch a StarPieceSpot waits for, from frame 120 (-DSPOT_TEST)
# cannon: with packs, Mario held in front of the first IronCannonLauncher(Point) from frame 120 (-DCANNON_TEST)
# shoot: with packs, star pieces out every 120 frames while the ShootingStars fall (-DSHOOT_TEST)
# plant: with packs, Mario walks through every member of the plant groups holding items (-DPLANT_TEST)
# stone: with packs, Mario spins at SmallStone rings in turn (-DSTONE_TEST)
# bee: with packs, Bee Mario jumps and flies with A held until his flight runs out (-DBEE_TEST)
# fire: with packs, Fire Mario spinning fire balls at a walker (-DFIRE_TEST)
# fat: with packs, star pieces shot at a TicoFat until it is full (-DFAT_TEST)
# walker: with packs, Mario by a Kuribo, Takobo, Karikari, Butterfly in turn (-DWALKER_TEST)
# route: with packs, the scenario played through: launch stars to its goal (EggStar 1-4), its action, its star (-DROUTE_TEST)
# purple: with packs, Mario takes every purple coin, then the 100-coin star (-DPURPLE_TEST)
# ota: with packs, Mario spins OtaKing's coconuts back until it goes down and takes its star (-DOTA_TEST)
# dino: with packs, Mario punches DinoPackun's tail ball until it goes down and takes its star (-DDINO_TEST)
# bubble: with packs, Mario over an AirBubbleGenerator with little oxygen (-DBUBBLE_TEST)
# swim: with packs, Mario dropped in a water area (-DSWIM_TEST)
# stray: with packs, Mario rescues the StrayTicos of a CollectTico (-DSTRAY_TEST)
# fountain: with packs, Mario on a FountainBig's base (-DFOUNTAIN_TEST)
# candle: with packs, Mario into candle flames and spinning by candlestands (-DCANDLE_TEST)
# screw: with packs, Mario spins on a ScrewSwitch (-DSCREW_TEST)
# jet: with packs, Mario takes a JetTurtle and throws it (-DJET_TEST)
# key: with packs, Mario onto each KeySwitch shown (-DKEY_TEST)
# note: with packs, a NoteFairy's notes out and taken one by one (-DNOTE_TEST)
# poihana: with packs, a Poihana chases, catches and throws Mario, then a spin (-DPOIHANA_TEST)
# unizo: with packs, Mario by a Unizo: it chases and hits him (-DUNIZO_TEST)
# stink: with packs, Mario in front of a StinkBugSmall, then a hip drop on it (-DSTINK_TEST)
# waterplant: with packs, Mario by the first water plant placed (-DWATERPLANT_TEST)
# fluff: with packs, a spin sends a Fluff up, Mario rides it and spins up (-DFLUFF_TEST)
# cocoon: with packs, Mario onto a SpaceCocoon, aimed with the pointer and launched (-DCOCOON_TEST)
# coco: with packs, a spin at a CocoNut, then Mario runs into one (-DCOCO_TEST)
# punch: with packs, a spin at a PunchingKinoko, then a hip drop onto it (-DPUNCH_TEST)
# biri: with packs, Mario held on a Birikyu's path (-DBIRI_TEST)
# giver: with packs, spins at DeadLeaves and touches of Pompon plants in turn (-DGIVER_TEST)
# jbranch: with packs, Mario jumps into a JumpBranch and swings (-DJBRANCH_TEST)
# ticorail: with packs, the first TicoRail's rail coordinate logged (-DTICORAIL_TEST)
# butterfly: with packs, the pointer on a Butterfly (star piece, it runs away) (-DBUTTERFLY_TEST)
# beeflower: with packs, Mario dropped onto BeeFlowerHovers (he goes through) (-DBEEFLOWER_TEST)
# teresa: with packs, Mario held by a Teresa (Boo), then a spin drifts it (-DTERESA_TEST)
# prom: with packs, Mario held on a LavaProminence's rail (-DPROM_TEST)
# karon: with packs, a Karon stomped: it breaks and comes back (-DKARON_TEST)
# kmini: with packs, a KuriboMini stomped, another spun (-DKMINI_TEST)
# tspot: with packs, star piece shots at TreasureSpots / CoinFlowers (-DTSPOT_TEST)
# shoot2: with packs, a star piece shot at a Takobo (stunned), then a stomp (-DSHOOT2_TEST)
# gcap: with packs, R2 with the cursor on a GCaptureTarget for 200 frames, then let go (-DGCAP_TEST)
# cursor: with packs, the pointer cursor sweeps the screen taking star pieces (-DCURSOR_TEST)
# flow: with packs, Mario put on the moving pieces of the first StarPieceFlow in turn (-DFLOW_TEST)
# karikari: with packs, Mario held by Karikari (they cling, bite), then a hip drop throws them off (-DKARIKARI_TEST)
# mkoopa: with packs, Mario held by a MechanicKoopaMini (it fires), then stomps it (-DMKOOPA_TEST)
# killer: with packs, Mario put 1500 in front of the first HomingKiller launcher, then left there (-DKILLER_TEST)
# meteor: with packs, Mario held half way down the first MeteorStrike's rail (-DMETEOR_TEST)
# takobo: with packs, Mario held by a Takobo (it punches), then stomps one and spins another (-DTAKOBO_TEST)
# kiraira: with packs, Mario put into the first Kiraira every 60 frames from 120 (-DKIRAIRA_TEST)
# tsuki: with packs, Mario put on the first Tsukidashikun at frame 120, its rail coordinate logged (-DTSUKI_TEST)
# warppod: with packs, Mario put into the first paired WarpPod at frames 120 and 700 (-DWARPPOD_TEST)
# jumpstand: with packs, Mario dropped on the first JumpStand at frame 120, in a hip drop at 400 (-DJUMPSTAND_TEST)
# assembly: with packs, Mario put over 20 AssemblyBlock places in turn, then left (-DASSEMBLY_TEST)
# geyser: with packs, Mario held in the first LavaGeyser's column from frame 120 (-DGEYSER_TEST)
# steam: with packs, Mario held in the first LavaSteam's vent from frame 120 (-DSTEAM_TEST)
# cage: with packs, Mario spins twice at the cage holding the first event power star, then goes for it (-DCAGE_TEST)
# flip: with packs, Mario put on each FlipPanel in turn from frame 120 (-DFLIP_TEST)
# hip: with packs, Mario hip drops onto the first HipDropRock (else breakable) at frame 120 (-DHIP_TEST)
# pipe: with packs, Mario dropped onto the first paired EarthenPipe at frame 120 (-DPIPE_TEST)
# zone: with packs, Mario caught by the first launch star at frame 120 (-DZONE_TEST)
# slope: Mario pushed into a steep slope (-DSLOPE_TEST), slice-slope.elf
# bench: scripted input path + frame-time summary (-DBENCH), written as slice-bench.elf
set -e
[ -d /mnt/d/smg-ps2 ] || { mkdir -p /mnt/d && mount -t drvfs D: /mnt/d; }
export PS2DEV=/usr/local/ps2dev PS2SDK=/usr/local/ps2dev/ps2sdk
export PATH=$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH
W=/root/smg-engine
rm -rf $W && mkdir -p $W/data
cp -r /mnt/d/smg-ps2/engine/src /mnt/d/smg-ps2/engine/Makefile $W/
cp ${2:-/mnt/d/smg-ps2/out/slice/hd2.bin} $W/data/slice.bin
cp $PS2SDK/iop/irx/libsd.irx $PS2SDK/iop/irx/audsrv.irx $W/data/
if [ -n "$3" ]; then echo "#define PACKS \"$3\"" > $W/src/packs_cfg.h; else : > $W/src/packs_cfg.h; fi
cd $W && B=${1#plain}; F=; [ "$B" = bench ] && F=-DBENCH; [ "$B" = slope ] && F=-DSLOPE_TEST; [ "$B" = zone ] && F=-DZONE_TEST; [ "$B" = box ] && F=-DBOX_TEST; [ "$B" = collapse ] && F=-DCOLLAPSE_TEST; [ "$B" = hazard ] && F=-DHAZARD_TEST; [ "$B" = item ] && F=-DITEM_TEST; [ "$B" = spin ] && F=-DSPIN_TEST; [ "$B" = pipe ] && F=-DPIPE_TEST; [ "$B" = hip ] && F=-DHIP_TEST; [ "$B" = flip ] && F=-DFLIP_TEST; [ "$B" = cage ] && F=-DCAGE_TEST; [ "$B" = steam ] && F=-DSTEAM_TEST; [ "$B" = geyser ] && F=-DGEYSER_TEST; [ "$B" = assembly ] && F=-DASSEMBLY_TEST; [ "$B" = jumpstand ] && F=-DJUMPSTAND_TEST; [ "$B" = warppod ] && F=-DWARPPOD_TEST; [ "$B" = tsuki ] && F=-DTSUKI_TEST; [ "$B" = kiraira ] && F=-DKIRAIRA_TEST; [ "$B" = takobo ] && F=-DTAKOBO_TEST; [ "$B" = meteor ] && F=-DMETEOR_TEST; [ "$B" = killer ] && F=-DKILLER_TEST; [ "$B" = mkoopa ] && F=-DMKOOPA_TEST; [ "$B" = karikari ] && F=-DKARIKARI_TEST; [ "$B" = flow ] && F=-DFLOW_TEST; [ "$B" = cursor ] && F=-DCURSOR_TEST; [ "$B" = gcap ] && F=-DGCAP_TEST; [ "$B" = shoot2 ] && F=-DSHOOT2_TEST; [ "$B" = tspot ] && F=-DTSPOT_TEST; [ "$B" = kmini ] && F=-DKMINI_TEST; [ "$B" = karon ] && F=-DKARON_TEST; [ "$B" = prom ] && F=-DPROM_TEST; [ "$B" = teresa ] && F=-DTERESA_TEST; [ "$B" = beeflower ] && F=-DBEEFLOWER_TEST; [ "$B" = butterfly ] && F=-DBUTTERFLY_TEST; [ "$B" = ticorail ] && F=-DTICORAIL_TEST; [ "$B" = jbranch ] && F=-DJBRANCH_TEST; [ "$B" = giver ] && F=-DGIVER_TEST; [ "$B" = biri ] && F=-DBIRI_TEST; [ "$B" = punch ] && F=-DPUNCH_TEST; [ "$B" = coco ] && F=-DCOCO_TEST; [ "$B" = cocoon ] && F=-DCOCOON_TEST; [ "$B" = fluff ] && F=-DFLUFF_TEST; [ "$B" = waterplant ] && F=-DWATERPLANT_TEST; [ "$B" = stink ] && F=-DSTINK_TEST; [ "$B" = unizo ] && F=-DUNIZO_TEST; [ "$B" = poihana ] && F=-DPOIHANA_TEST; [ "$B" = note ] && F=-DNOTE_TEST; [ "$B" = key ] && F=-DKEY_TEST; [ "$B" = jet ] && F=-DJET_TEST; [ "$B" = screw ] && F=-DSCREW_TEST; [ "$B" = candle ] && F=-DCANDLE_TEST; [ "$B" = fountain ] && F=-DFOUNTAIN_TEST; [ "$B" = stray ] && F=-DSTRAY_TEST; [ "$B" = swim ] && F=-DSWIM_TEST; [ "$B" = bubble ] && F=-DBUBBLE_TEST; [ "$B" = dino ] && F=-DDINO_TEST; [ "$B" = ota ] && F=-DOTA_TEST; [ "$B" = purple ] && F=-DPURPLE_TEST; [ "$B" = route ] && F=-DROUTE_TEST; [ "$B" = walker ] && F=-DWALKER_TEST; [ "$B" = fat ] && F=-DFAT_TEST; [ "$B" = fire ] && F=-DFIRE_TEST; [ "$B" = bee ] && F=-DBEE_TEST; [ "$B" = stone ] && F=-DSTONE_TEST; [ "$B" = plant ] && F=-DPLANT_TEST; [ "$B" = kgun ] && F=-DKGUN_TEST; [ "$B" = orock ] && F=-DOROCK_TEST; [ "$B" = hhp ] && F=-DHHP_TEST; [ "$B" = ibubble ] && F=-DIBUBBLE_TEST; [ "$B" = shoot ] && F=-DSHOOT_TEST; [ "$B" = cannon ] && F=-DCANNON_TEST; [ "$B" = spot ] && F=-DSPOT_TEST; [ "$B" = restart ] && F=-DRESTART_TEST; [ "$B" = cover ] && F=-DCOVER_TEST; [ "$B" = enemy ] && F=-DENEMY_TEST; [ "$B" = rotor ] && F=-DROTOR_TEST; [ "$B" = rail ] && F=-DRAIL_TEST; [ "$B" = overview ] && F=-DOVERVIEW_TEST; [ "$4" = disc ] && F="$F -DDISC"; [ -n "$TEASER" ] && F="$F -DTEASER"; [ -n "$XF" ] && F="$F $XF"; make -s EXTRA="$F" 2>&1 | grep -v "^$" || true
test -f slice.elf
cp slice.elf /mnt/d/smg-ps2/engine/slice${B:+-$B}.elf
[ -n "$3" ] && [ "$4" != disc ] && cp slice.elf /mnt/d/smg-ps2/out/slice-packs${B:+-$B}.elf
[ "$4" = disc ] && mkdir -p /mnt/d/smg-ps2/out/disc && cp slice.elf /mnt/d/smg-ps2/out/disc/SLICE.ELF
ls -l slice.elf
mips64r5900el-ps2-elf-size slice.elf
