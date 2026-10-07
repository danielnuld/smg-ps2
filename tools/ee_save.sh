#!/bin/bash
# Save the last EE compile lists into out/ee (sorted).
mkdir -p /mnt/d/smg-ps2/out/ee
sort /root/ee/ok.txt > /mnt/d/smg-ps2/out/ee/ok.txt; sort /root/ee/fail.txt > /mnt/d/smg-ps2/out/ee/fail.txt
wc -l /mnt/d/smg-ps2/out/ee/*.txt
