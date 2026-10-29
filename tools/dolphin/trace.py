"""Every frame of Mario's state in the Wii while a movie plays (Dolphin pauses at the movie's end).
Polls Petari's Mario (RMGE01; found by his place at the start: the heap moves between runs) and keeps each frame's state; the last is
the movie's last frame (paused), so with the movie ending at g = G (replay.py dtm ... 0) frame k from the end is g G - k.
usage: python -I trace.py OUT.csv SECONDS [G] ["x y z": where Mario stands at the start]
columns: g, pos, front, walk, jumpvec, shadow, gnd (mGroundPos), vspeed (mVerticalSpeed), _368, states (mMovementStates
0x8..0x10 hex), _1C, _3C0, _3CE, _3BC, _41C, _8F8, _960; MarioActor mPosition, mVelocity, _288, _294, _F44
"""
import struct
import sys
import time

import numpy as np
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dolmem  # noqa: E402

FIELDS = [("pos", 0x130, "3f"), ("front", 0x208, "3f"), ("walk", 0x278, "f"), ("jv", 0x2D4, "3f"),
          ("shadow", 0x31C, "3f"), ("gnd", 0x48C, "3f"), ("vspeed", 0x488, "f"), ("n368", 0x368, "3f"),
          ("st", 0x8, "8s"), ("d1C", 0x1C, "4s"), ("_3C0", 0x3C0, "H"), ("_3CE", 0x3CE, "H"), ("_3BC", 0x3BC, "H"), ("_41C", 0x41C, "H"), ("_8F8", 0x8F8, "3f"),
          ("_960", 0x960, "H")]
# MarioActor (Mario's mActor at +4): mPosition, mVelocity, _288 (the binder's push), _294, _F44 (bind on)
ACTOR = [(0x0C, 0x3C), (0x288, 0x2A0), (0xF44, 0xF45)]
AFIELDS = [("apos", 0, "3f"), ("avel", 12, "3f"), ("a288", 0x30, "3f"), ("a294", 0x3C, "3f"), ("aF44", 0x48, "B")]


def parse(b):
    out = []
    for name, off, f in FIELDS + [(n, 0x980 + o, f) for n, o, f in AFIELDS]:
        v = struct.unpack_from(">" + f, b, off)
        out.append(" ".join(x.hex() if isinstance(x, bytes) else f"{x:.3f}" if isinstance(x, float) else str(x) for x in v))
    return out


def main():
    out, secs = sys.argv[1], float(sys.argv[2])
    g_end = int(sys.argv[3]) if len(sys.argv) > 3 else None
    start = [float(v) for v in sys.argv[4].split()] if len(sys.argv) > 4 else [-3303.3, -13081.9, -15352.9]
    end = time.time() + secs
    ram = None
    while ram is None and time.time() < end:
        try:
            ram = dolmem.Ram()
        except Exception:
            time.sleep(1)
    # Mario moves with the heap from run to run: mPosition at his start (within 2), unit mFrontVec and _368, a walk
    # speed, on the ground
    addr = None
    while addr is None and time.time() < end:
        time.sleep(2)
        m = np.frombuffer(ram.read(0x80000000, 0x1800000), dtype=">f4")
        hit = np.nonzero((np.abs(m[:-2] - start[0]) < 2) & (np.abs(m[1:-1] - start[1]) < 2) & (np.abs(m[2:] - start[2]) < 2))[0]
        for i in hit:
            b = int(i) - 0x130 // 4
            f, n, w = m[b + 0x208 // 4:][:3], m[b + 0x368 // 4:][:3], m[b + 0x278 // 4]
            st = ram.read(0x80000000 + 4 * b + 8, 1)
            if (b >= 0 and abs(float(np.dot(f, f)) - 1.0) < 0.01 and abs(float(np.dot(n, n)) - 1.0) < 0.01
                    and 0.0 <= w <= 2.0 and st and st[0] & 0x40):  # front, ground normal, walk speed, on the ground
                addr = 0x80000000 + 4 * b
                break
    print("Mario at", hex(addr) if addr else None)
    # a frame's state: the same bytes read twice 2 ms apart (a read in the middle of the game's update is torn);
    # Mario's timers (_3CE on the ground, _3BC in the air) change it every frame
    snaps, prev, last, changed = [], None, None, time.time()
    while addr and time.time() < end and time.time() - changed < 20:  # paused at the movie's end: 20 s unchanged
        try:
            b = ram.read(addr, 0x980)
            act = struct.unpack_from(">I", b, 4)[0]
            b += b"".join(ram.read(act + lo, hi - lo) for lo, hi in ACTOR)
        except Exception:
            break
        if b and b == prev and b != last:
            snaps.append(b)
            last, changed = b, time.time()
        prev = b
        time.sleep(0.002)
    with open(out, "w", newline="\n") as f:
        for k, b in enumerate(snaps):
            g = g_end - (len(snaps) - 1 - k) if g_end is not None else k
            f.write(",".join([str(g)] + parse(b)) + "\n")
    print(f"{len(snaps)} frames -> {out}")


if __name__ == "__main__":
    main()
