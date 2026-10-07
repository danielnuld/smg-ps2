"""Named heap use from Dolphin MEM1/MEM2 dumps, via the game's own HeapMemoryWatcher.

HeapMemoryWatcher keeps 10 heap pointers in this order (Petari HeapMemoryWatcher.cpp:157-159): stationed MEM1/MEM2,
game MEM1/MEM2, file cache, scene MEM1/MEM2, WPAD, HOME button layout, audio system (solid, 0x1E0000, .cpp:142).
The table is found by the audio heap object (the solid heap of size 0x1E0000 that heapwalk finds) and checked by
requiring every entry to be a heap object heapwalk found.

usage: python tools/heapreport.py mem1.raw mem2.raw [label]
"""
import struct
import sys

from heapwalk import chains, heaps

NAMES = ("stationed_mem1", "stationed_mem2", "game_mem1", "game_mem2", "file_cache", "scene_mem1", "scene_mem2",
         "wpad", "home_button", "audio")


def report(dumps):
    hs = heaps(dumps, chains(dumps))
    by_obj = {h["obj"]: h for h in hs}
    audio = [h for h in hs if h["kind"] == "solid" and 0x1D0000 <= h["size"] <= 0x1E0000]  # size = mEnd - mStart (no header)
    mem1 = dumps[0]
    for a in audio:
        i = mem1.find(struct.pack(">I", a["obj"]))
        while i >= 0:
            base = i - 9 * 4
            ptrs = struct.unpack(">10I", mem1[base:base + 40])
            if all(p in by_obj for p in ptrs):
                return {n: by_obj[p] for n, p in zip(NAMES, ptrs)}
            i = mem1.find(struct.pack(">I", a["obj"]), i + 1)
    raise SystemExit("HeapMemoryWatcher table not found")


if __name__ == "__main__":
    dumps = [open(p, "rb").read() for p in sys.argv[1:3]]
    label = sys.argv[3] if len(sys.argv) > 3 else ""
    M = 2**20
    for n, h in report(dumps).items():
        print(f"{label:28} {n:15} {h['kind']:5} size {h['size'] / M:6.2f} MB  used {h['used'] / M:6.2f} MB")
