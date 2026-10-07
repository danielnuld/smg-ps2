#!/bin/bash
# Show context for TUs whose .err matches a pattern. usage: ee_grep.sh <pattern> [n]
grep -l "$1" /root/ee/obj/*.err | head -${2:-2} | while read f; do echo "== $f"; grep -B2 -A3 -m1 "$1" "$f"; done
