"""Music streams (milestone 6, music through PCM streams as phase 0c decided): AudioRes/Stream/*.ast -> PS2 PCM.

usage: python tools/music.py data/fs/files out/music [names...]   (no names: every stream)
       python tools/music.py --selftest

AST (Wii stream): header 'STRM' {u32 data size, u16 format (1 = PCM16), u16 bits, u16 channels, u16 ?, u32 rate,
u32 samples, u32 loop start, u32 loop end, u32 first block size} then blocks 'BLCK' {u32 size per channel, 0x18 pad}
followed by each channel's `size` bytes of big-endian PCM16. Output <name>.pcm: u32 'SMGM', u32 rate, u32 channels,
u32 loop start byte offset (into the samples), then interleaved little-endian PCM16 (what audsrv plays). Channels
beyond the first two (the *_multi tracks layer instruments) are dropped. ponytail: no resampling (32 kHz stays).
"""
import struct
import sys
from pathlib import Path

import numpy as np


def convert(ast):
    magic, size, fmt, bits, chans, _, rate, samples, loop_start, loop_end = struct.unpack_from(">4sIHHHHIIII", ast, 0)
    assert magic == b"STRM" and fmt == 1 and bits == 16, (magic, fmt, bits)
    out, o = [], 0x40
    while o + 0x20 <= len(ast) and ast[o:o + 4] == b"BLCK":
        n = struct.unpack_from(">I", ast, o + 4)[0]
        o += 0x20
        chs = [np.frombuffer(ast, ">i2", n // 2, o + c * n) for c in range(chans)]
        o += n * chans
        out.append(np.stack(chs[:2] if chans >= 2 else [chs[0], chs[0]], axis=1))
    pcm = np.concatenate(out)[:samples].astype("<i2")
    return rate, loop_start * 4, pcm.tobytes()


def main(fs, out, names):
    src = Path(fs) / "AudioRes" / "Stream"
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for p in sorted(src.glob("*.ast")):
        name = p.stem.replace(".ast", "")
        if names and name not in names:
            continue
        rate, loop, data = convert(p.read_bytes())
        (out / f"{name}.pcm").write_bytes(struct.pack("<4sIII", b"SMGM", rate, 2, loop) + data)
        print(f"{name}: {rate} Hz, {len(data) / 4 / rate:.1f} s, loop at {loop / 4 / rate:.1f} s, {len(data) >> 10} KB")


def selftest():
    # two blocks of 2 samples, 2 channels: L = 1, 2 | 5, 6 ; R = 3, 4 | 7, 8
    hdr = struct.pack(">4sIHHHHIIIII", b"STRM", 0, 1, 16, 2, 0xFFFF, 32000, 4, 1, 4, 4)
    hdr += b"\0" * (0x40 - len(hdr))
    blk = lambda l, r: b"BLCK" + struct.pack(">I", 4) + b"\0" * 0x18 + struct.pack(">2h", *l) + struct.pack(">2h", *r)
    rate, loop, data = convert(hdr + blk((1, 2), (3, 4)) + blk((5, 6), (7, 8)))
    assert rate == 32000 and loop == 4 and np.frombuffer(data, "<i2").tolist() == [1, 3, 2, 4, 5, 7, 6, 8]
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        main(sys.argv[1], sys.argv[2], sys.argv[3:])
