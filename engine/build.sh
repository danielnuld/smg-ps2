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
cd $W && B=${1#plain}; F=; [ "$B" = bench ] && F=-DBENCH; [ "$B" = slope ] && F=-DSLOPE_TEST; [ "$B" = zone ] && F=-DZONE_TEST; [ "$B" = box ] && F=-DBOX_TEST; [ "$B" = collapse ] && F=-DCOLLAPSE_TEST; [ "$B" = hazard ] && F=-DHAZARD_TEST; [ "$B" = item ] && F=-DITEM_TEST; [ "$B" = spin ] && F=-DSPIN_TEST; [ "$B" = pipe ] && F=-DPIPE_TEST; [ "$B" = hip ] && F=-DHIP_TEST; [ "$B" = flip ] && F=-DFLIP_TEST; [ "$B" = cover ] && F=-DCOVER_TEST; [ "$B" = enemy ] && F=-DENEMY_TEST; [ "$B" = rotor ] && F=-DROTOR_TEST; [ "$B" = rail ] && F=-DRAIL_TEST; [ "$B" = overview ] && F=-DOVERVIEW_TEST; [ "$4" = disc ] && F="$F -DDISC"; [ -n "$TEASER" ] && F="$F -DTEASER"; make -s EXTRA="$F" 2>&1 | grep -v "^$" || true
test -f slice.elf
cp slice.elf /mnt/d/smg-ps2/engine/slice${B:+-$B}.elf
[ -n "$3" ] && [ "$4" != disc ] && cp slice.elf /mnt/d/smg-ps2/out/slice-packs${B:+-$B}.elf
[ "$4" = disc ] && mkdir -p /mnt/d/smg-ps2/out/disc && cp slice.elf /mnt/d/smg-ps2/out/disc/SLICE.ELF
ls -l slice.elf
mips64r5900el-ps2-elf-size slice.elf
