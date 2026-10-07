#!/bin/bash
# Sum of .text over individual objects vs. the ld -r result: shows COMDAT dedup happened.
S=/usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-size
sum=0; for o in $(sed 's#/#_#g; s#^#/root/ee/obj/#; s#$#.o#' /root/ee/ok.txt); do t=$($S -A $o | awk '$1 ~ /^\.text/ {s+=$2} END {print s+0}'); sum=$((sum+t)); done
echo "sum of objects .text: $sum"
echo "ld -r .text:          $($S -A /root/ee/all.o | awk '$1 ~ /^\.text/ {s+=$2} END {print s+0}')"
