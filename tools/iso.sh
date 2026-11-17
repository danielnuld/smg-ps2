#!/bin/bash
# The disc image built on the WSL disk and sent to nuld. On D: (drvfs) disc.py took half an hour to write PACKS.BIN
# and scp sent all 3.5 GB each time; here PACKS.BIN and the ISO are written on ext4 and rsync sends only the blocks
# that changed (an engine-only fix: a few MB).
# usage (from Windows): wsl.exe -e bash /mnt/d/smg-ps2/tools/iso.sh [host: nuld (default), nuld-ts away from home, -: no upload]
# before it: engine/build.sh MODE blob Galaxy/n disc (out/disc/SLICE.ELF; without one the last disc ELF is used again)
# -> ~/smg-repack/out/smg-ps2.iso (not copied to D:: PCSX2 does not read the full DVD image anyway)
set -e
S=$(cd "$(dirname "$0")/.." && pwd)
W=~/smg-repack
PY=~/smgvenv/bin/python
H=${1:-nuld}
SSH="/mnt/c/Windows/System32/OpenSSH/ssh.exe -o BatchMode=yes"   # the Windows client: its keys and its config's hosts
DST="/srv/data/ps2/DVD/Super Mario Galaxy.iso"
step() { echo "== $1 $(date +%T)"; }

step "mirror in"
mkdir -p $W/out/disc $W/out/packs $W/tools $W/engine/src $W/vendor/Petari $W/bin
rsync -a --delete --exclude __pycache__ $S/tools/ $W/tools/
rsync -a --delete $S/engine/src/ $W/engine/src/   # (disc.py's zone_fit imports slice.py: what it reads)
rsync -a --delete --exclude .git $S/vendor/Petari/ $W/vendor/Petari/
rsync -a $S/bin/meshopt.so $W/bin/
rsync -a --modify-window=2 $S/out/packs/ $W/out/packs/   # (movies and music too)
[ -f $S/out/disc/SLICE.ELF ] && mv -f $S/out/disc/SLICE.ELF $S/out/disc/SMGP_000.01   # as disc.py does: one boot file
cp $S/out/disc/SMGP_000.01 $W/out/disc/
cd $W

step disc
$PY tools/disc.py
size=$(stat -c %s out/smg-ps2.iso)
[ "$H" = - ] && { step done; exit 0; }

step "upload to $H"
# rsync writes a temporary file beside the old ISO and renames it once whole (Orbit's catalog never sees half of one):
# it needs the ISO's size free
avail=$($SSH $H "df -B1 --output=avail /srv/data/ps2/DVD | tail -1" | tr -d '\r ')
[ "$avail" -gt $((size + 500000000)) ] || { echo "iso.sh: $H has $avail bytes free, the ISO takes $size"; exit 1; }
rsync --partial --info=stats1 -e "$SSH" out/smg-ps2.iso "$H:$DST"
there=$($SSH $H "stat -c %s '$DST'" | tr -d '\r ')
[ "$there" = "$size" ] || { echo "iso.sh: $there bytes on $H, $size here"; exit 1; }
# udpfs keeps the replaced ISO open (deleted but held) and serves it on: restarted, unless the console read from it
# in the last 5 minutes (someone is playing: restart it by hand later, systemctl --user restart udpfs)
$SSH $H 'last=$(journalctl --user -u udpfs -n 1 -o short-unix --no-pager | tail -1 | cut -d. -f1); idle=$(( $(date +%s) - ${last:-0} ));
if [ $idle -gt 300 ]; then systemctl --user restart udpfs && echo "udpfs restarted (idle $idle s)"; else echo "udpfs NOT restarted: read from $idle s ago"; fi'
step "done: $size bytes on $H"
