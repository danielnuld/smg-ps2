#!/bin/bash
# Memory check without the emulator: what an ELF leaves for the packs' region (main.c pk_init: the heap less 1.5 MB,
# in 256 KB steps), against a baseline ELF (default: the last release's, out/disc/SLICE.ELF).
# usage (WSL): bash tools/memcheck.sh <elf> [baseline elf]   -> exit 1 if the region is smaller than the baseline's
# The EE has 32 MB; the heap is what lies between _end and the stack (linkfile: 128 KB). The packs fitting the region
# (PACK_ROOM) is slice_all's check, at repack time.
set -e
NM=/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-nm
D=$(cd "$(dirname "$0")/.." && pwd)
region() { # <elf> -> "_end heap region" in bytes
    local end=$((16#$($NM "$1" | awk '$3 == "_end" {print $1}')))
    local heap=$(( (32 << 20) - (128 << 10) - end ))
    echo $end $heap $(( (heap - (1536 << 10)) / (256 << 10) * (256 << 10) ))
}
read e h r < <(region "$1")
read be bh br < <(region "${2:-$D/out/disc/SLICE.ELF}")
printf '%-9s _end %8d  heap %8d  region %5d KB\n' elf $e $h $((r >> 10)) baseline $be $bh $((br >> 10))
printf '_end %+d B against the baseline; %d B more before the region loses 256 KB\n' $((e - be)) $(( (h - (1536 << 10)) % (256 << 10) ))
[ $r -ge $br ] || { echo "FAIL: the packs' region is $(( (br - r) >> 10 )) KB smaller than the baseline's"; exit 1; }
