#!/bin/bash
# Release repack on the WSL disk: lyt.py (the menus' packs), slice_all.py (resident + scenarios), zone_fit.py, every
# sound pack. Through drvfs (/mnt/d) a third of a repack went in file calls, and the sound packs were built one by one
# (2026-11-09: 99 min). The inputs are mirrored to ~/smg-repack (rsync: only what changed), the packs built there and
# the ones that changed copied back to out/packs (movies and music there are left alone).
# usage (from Windows): wsl.exe -e bash /mnt/d/smg-ps2/tools/release.sh [Galaxy,Galaxy...: only those] [jobs]
# JOBS: slice_all takes ~1.5 GB a job at its peak; the sound packs are light.
set -e
S=$(cd "$(dirname "$0")/.." && pwd)
W=~/smg-repack
PY=~/smgvenv/bin/python
ONLY=$1
J=${2:-$(nproc)}
step() { echo "== $1 $(date +%T)"; }

step "mirror in"
mkdir -p $W/out/sound_cache
. $S/tools/wsl_mirror.sh
[ -d $S/out/sound_cache ] && rsync -a --ignore-existing $S/out/sound_cache/ $W/out/sound_cache/
# the packs other tools make (letter.bin, title.bin...: slice_all's room counts the menus') and, for a partial
# repack, the scenarios left alone (zone_fit reads them all)
rsync -a --modify-window=2 --exclude movies --exclude music $S/out/packs/ $W/out/packs/
cd $W

step lyt
$PY tools/lyt.py data/fs/files
step slice_all
$PY tools/slice_all.py data/fs/files out/packs $J $ONLY
step zone_fit
$PY tools/zone_fit.py out/packs || true
step "sound shared"   # first and alone: it writes se_ids.h / mario_snd.h, the scenarios' packs then find them unchanged
$PY tools/sound.py --pack data/fs/files out/packs/sound.bin engine/src/se_ids.h 16000 engine/src/game.c
step "sound scenarios"
find out/packs -mindepth 3 -maxdepth 3 -name scene.bin -printf '%h\n' | sort |
    { [ -z "$ONLY" ] && cat || grep -E "/(${ONLY//,/|})/"; } |
    xargs -P $J -I{} sh -c "$PY tools/sound.py --pack data/fs/files {}/sound.bin engine/src/se_ids.h 16000 --scene {}/scene.bin engine/src/game.c > {}/sound.log 2>&1 || echo 'sound pack failed: {}'"
grep -L "^sound.bin:" $(find out/packs -mindepth 3 -maxdepth 3 -name sound.log) | sed 's/^/no sound.bin line: /'

step "mirror out"
# by content (-c): a pack rebuilt the same is not written again (drvfs writes are the slow part; disc.py reads them all anyway)
rsync -rtc --modify-window=2 --exclude sound.log --out-format='%n' out/packs/ $S/out/packs/ | grep -c . | sed 's/^/packs changed: /'
rsync -rc engine/src/se_ids.h engine/src/mario_snd.h $S/engine/src/
rsync -a --ignore-existing out/sound_cache/ $S/out/sound_cache/
step done
