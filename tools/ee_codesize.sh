#!/bin/bash
# Task 4.2: compile every decomp TU for the EE, then `ld -r` them so shared inlines/templates count once.
# usage (WSL): bash tools/ee_codesize.sh <jobs> <tar of sources> [only-these-TUs.txt]   -> out/ee/{ok,fail}.txt, out/ee/all.o
set -u
[ -d /mnt/d/smg-ps2 ] || { mkdir -p /mnt/d && mount -t drvfs D: /mnt/d; }
D=/mnt/d/smg-ps2
# headers off the SD card through drvfs are ~40x slower: build from a copy on the WSL disk
export SRC=/root/smg-src
# pack on Windows first (fast there): tar -cf <tar> --exclude=.git vendor/galaxy-macos vendor/aurora/include tools/ee_cc.sh tools/ee_include
rm -rf $SRC && mkdir -p $SRC && tar -xf "$2" -C $SRC
# ponytail: 0x318 is galaxy-macos' 64-bit offset; the assert only guards layout, code size is the same without it
sed -i '/update JAS_TRACK_NODE_OFFSET/d' $SRC/vendor/galaxy-macos/libs/JSystem/include/JSystem/JAudio2/JASTrack.hpp
R=$D; G=$SRC/vendor/galaxy-macos; O=/root/ee/obj; mkdir -p $O $R/out/ee
# optional 3rd arg: only rebuild the TUs listed there (e.g. the previous fail.txt), keeping earlier successes
if [ -n "${3:-}" ]; then cp "$3" /root/ee/list.txt; cp $R/out/ee/ok.txt /root/ee/ok.txt
else grep -oE 'src/[^ )]+\.(cpp|c)' $G/port/sources.cmake | grep -E '\.cpp$' > /root/ee/list.txt; : > /root/ee/ok.txt; fi
: > /root/ee/fail.txt
build() { o=/root/ee/obj/$(echo "$1" | tr / _).o
  if bash $SRC/tools/ee_cc.sh "$1" "$o" 2>/root/ee/obj/$(echo "$1" | tr / _).err; then echo "$1" >> /root/ee/ok.txt; else echo "$1" >> /root/ee/fail.txt; fi; }
export -f build
xargs -a /root/ee/list.txt -P ${1:-8} -I{} bash -c 'build {}'
sort -o /root/ee/ok.txt /root/ee/ok.txt; sort -o /root/ee/fail.txt /root/ee/fail.txt
cp /root/ee/ok.txt /root/ee/fail.txt $R/out/ee/
/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-ld -r -o /root/ee/all.o $(sed 's#/#_#g; s#^#/root/ee/obj/#; s#$#.o#' /root/ee/ok.txt)
/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-size -A /root/ee/all.o > $R/out/ee/all_size.txt
wc -l /root/ee/list.txt /root/ee/ok.txt /root/ee/fail.txt
