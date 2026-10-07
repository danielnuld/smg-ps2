# Phase-1 slice engine. Build in WSL with the ps2dev toolchain (/usr/local/ps2dev):
#   bash engine/build.sh          (copies the tree to the WSL disk, builds, copies slice.elf back)
EE_BIN = slice.elf
EE_OBJS = src/main.o src/mem.o src/blob.o src/render.o src/pad.o src/gravity.o src/kcl.o src/game.o src/vu1_flat.o src/assets.o

EE_INCS += -Isrc
EE_LIBS += -lpacket2 -ldraw -lgraph -ldma -lpad -lpatches -ldebug -lm
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -ffast-math $(EXTRA)

all: $(EE_BIN)

src/assets.o: src/assets.S data/slice.bin

# VU1 microprogram (ps2sdk's sample Makefile does the same with dvp-as)
%.o: %.vsm
	dvp-as -o $@ $<

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
