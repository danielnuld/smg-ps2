# Phase-1 slice engine. Build in WSL with the ps2dev toolchain (/usr/local/ps2dev):
#   bash engine/build.sh          (copies the tree to the WSL disk, builds, copies slice.elf back)
EE_BIN = slice.elf
EE_OBJS = src/main.o src/mem.o src/blob.o src/render.o src/pad.o src/gravity.o src/kcl.o src/game.o src/anim.o src/camera.o src/vu1_tex.o src/assets.o src/audio.o src/se.o src/se_seq.o src/fx.o

EE_INCS += -Isrc -I$(PS2SDK)/ports/include
EE_LIBS += -L$(PS2SDK)/ports/lib -lz -lcdvd -laudsrv -lpacket2 -ldraw -lgraph -ldma -lpad -lpatches -ldebug -lm
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -ffast-math $(EXTRA)

all: $(EE_BIN)

src/assets.o: src/assets.S data/slice.bin data/libsd.irx data/audsrv.irx

# VU1 microprogram (ps2sdk's sample Makefile does the same with dvp-as)
%.o: %.vsm
	dvp-as -o $@ $<

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
