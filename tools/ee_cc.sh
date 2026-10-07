#!/bin/bash
# Compile one decomp TU for the EE (code-size sampling, task 4.2). Flags mirror galaxy-macos port/CMakeLists.txt:81-99
# minus clang-only ones; ee_include/ shims newlib/libstdc++ clashes. usage: tools/ee_cc.sh <src under galaxy-macos> <out.o> [extra flags]
R=${SRC:-/mnt/d/smg-ps2}; G=$R/vendor/galaxy-macos; T=$R/tools/ee_include
src=$1 out=$2; shift 2
exec /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-g++ -c -Os -G0 -std=gnu++17 -fno-exceptions -fno-rtti \
  -fshort-wchar -funsigned-char -fno-strict-aliasing -fwrapv -w -Werror=return-type -Umips \
  -DTARGET_PC=1 -DPORT_AURORA=1 -DVERSION=0 -include $G/port/include/port/prelude.h -include $T/ee_prelude.h \
  -I$T -I$G/port/include -I$G/include -I$G/libs/JSystem/include -I$G/libs/RVLFaceLib/include \
  -I$G/libs/RVL_SDK/include -I$G/libs/nw4r/include -I$R/vendor/aurora/include "$@" "$G/$src" -o "$out"
