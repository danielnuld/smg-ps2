# Phase-1 slice engine. Build in WSL with the ps2dev toolchain (/usr/local/ps2dev):
#   bash engine/build.sh          (copies the tree to the WSL disk, builds, copies slice.elf back)
EE_BIN = slice.elf
EE_OBJS = src/main.o src/mem.o src/blob.o src/render.o src/pad.o src/assets.o

GSKITDIR ?= $(PS2DEV)/gsKit
EE_INCS += -I$(GSKITDIR)/include -Isrc
EE_LDFLAGS += -L$(GSKITDIR)/lib
EE_LIBS += -lgskit_toolkit -lgskit -ldmakit -lpad -lpatches -ldebug -lm
EE_CFLAGS += -O2 -Wall -Wextra -std=gnu99 -ffast-math

all: $(EE_BIN)

src/assets.o: src/assets.S data/slice.bin

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
