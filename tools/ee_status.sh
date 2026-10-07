#!/bin/bash
echo "cc1plus: $(pgrep -c cc1plus)  rsync: $(pgrep -c rsync)  xargs: $(pgrep -c xargs)"
ls /root/smg-src/vendor 2>&1; du -sh /root/smg-src 2>/dev/null
wc -l /root/ee/ok.txt /root/ee/fail.txt
