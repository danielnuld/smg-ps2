#!/bin/bash
# Host tests for the engine's portable C (gravity, collision, ...). usage: wsl.exe -e bash /mnt/d/smg-ps2/engine/test/run.sh
set -e
[ -d /mnt/d/smg-ps2 ] || { mkdir -p /mnt/d && mount -t drvfs D: /mnt/d; }
E=$(cd "$(dirname "$0")/.." && pwd)   # the engine this script lives in (a git worktree tests itself)
T=/root/smg-engine-test$( [ "$E" = /mnt/d/smg-ps2/engine ] || echo "-$(basename "$(dirname "$E")")" )
rm -rf $T && mkdir -p $T && cp -r $E/src $E/test $T/
cd $T/test
for t in test_*.c; do
  n=${t%.c}
  srcs=$(sed -n 's|^// SRCS: ||p' $t | tr -d '\r')   # (a Windows checkout's CRLF)
  gcc -O1 -std=gnu99 -Wall -o $n $t ../src/${n#test_}.c $srcs -lm
  ./$n
done
