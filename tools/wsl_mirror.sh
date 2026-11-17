# Sourced by release.sh and iso.sh (S: the repo, W: the work directory on the WSL disk): mirrors there what the tools
# read, only what changed.
# The game's files and Petari go on a FAT image mounted in W: the tools name some files in another case than the
# disc's (LayoutData/SignBoardBalloon.arc...). Windows does not care; on ext4 the file is "not there", and where the
# tools ask exists() a model would be left out without a word. The WSL kernel has no ext4 casefold; vfat it has.
CI=$W/ci
mkdir -p $CI $W/tools $W/engine/src $W/bin $W/out/packs $W/out/disc
if ! mountpoint -q $CI; then
    if [ ! -f $W/ci.img ]; then
        command -v mkfs.vfat >/dev/null || apt-get install -y -q dosfstools >/dev/null
        truncate -s 6G $W/ci.img && mkfs.vfat -F 32 $W/ci.img >/dev/null
    fi
    mount -o loop,noatime $W/ci.img $CI
fi
mkdir -p $CI/data/fs/files $CI/vendor/Petari
for d in data vendor; do
    [ -L $W/$d ] || rm -rf $W/$d   # (a plain ext4 copy from before the image)
    ln -sfn $CI/$d $W/$d
done
rsync -rt --modify-window=2 --delete $S/data/fs/files/ $CI/data/fs/files/
rsync -rt --modify-window=2 --delete $S/data/fs/sys $CI/data/fs/   # (lyt.py: main.dol's tables)
rsync -rt --modify-window=2 --delete --exclude .git $S/vendor/Petari/ $CI/vendor/Petari/
rsync -a --delete --exclude __pycache__ $S/tools/ $W/tools/
rsync -a --delete $S/engine/src/ $W/engine/src/
rsync -a $S/bin/meshopt.so $W/bin/
