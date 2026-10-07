#!/bin/bash
# First errors of each failed TU, grouped by message (tools/ee_codesize.sh output).
for f in $(cat /root/ee/fail.txt); do grep -m1 -E "error" /root/ee/obj/$(echo $f | tr / _).err | sed -E 's/^[^ ]+ error: //'; done | sort | uniq -c | sort -rn | head -${1:-25}
wc -l /root/ee/ok.txt /root/ee/fail.txt
