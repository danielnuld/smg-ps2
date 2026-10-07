#!/bin/bash
# Build the slice ELF in WSL (the SD card through drvfs is slow, so build on the WSL disk).
# usage (from Windows): wsl.exe -e bash /mnt/d/smg-ps2/engine/build.sh [bench|plain|slope] [blob, default out/slice/hd2.bin] [Galaxy/n]
# Galaxy/n: boot from the streaming packs in out/packs (host:packs/) instead of the linked blob; the ELF is
#           also copied to out/slice-packs[-bench].elf, run it from there
# zone: with packs, Mario teleported next to a low-loaded zone at frame 120 (-DZONE_TEST)
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
if [ -n "$3" ]; then echo "#define PACKS \"$3\"" > $W/src/packs_cfg.h; else : > $W/src/packs_cfg.h; fi
cd $W && B=${1#plain}; F=; [ "$B" = bench ] && F=-DBENCH; [ "$B" = slope ] && F=-DSLOPE_TEST; [ "$B" = zone ] && F=-DZONE_TEST; make -s EXTRA=$F 2>&1 | grep -v "^$" || true
test -f slice.elf
cp slice.elf /mnt/d/smg-ps2/engine/slice${B:+-$B}.elf
[ -n "$3" ] && cp slice.elf /mnt/d/smg-ps2/out/slice-packs${B:+-$B}.elf
ls -l slice.elf
mips64r5900el-ps2-elf-size slice.elf
