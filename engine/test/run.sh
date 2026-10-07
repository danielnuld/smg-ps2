#!/bin/bash
# Host tests for the engine's portable C (gravity, collision, ...). usage: wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh
set -e
[ -d /mnt/d/smg-ps2 ] || { mkdir -p /mnt/d && mount -t drvfs D: /mnt/d; }
E=/mnt/d/smg-ps2/engine
T=/root/smg-engine-test; rm -rf $T && mkdir -p $T && cp -r $E/src $E/test $T/
cd $T/test
for t in test_*.c; do
  n=${t%.c}
  srcs=$(sed -n 's|^// SRCS: ||p' $t)
  gcc -O1 -std=gnu99 -Wall -o $n $t ../src/${n#test_}.c $srcs -lm
  ./$n
done
