"""The game's THP movies (MovieData/*.thp, MJPEG 640 x 368 at 59.94 fps, THP ADPCM 32 kHz stereo) for the PS2's IPU:
<out>/movies/<Name>.smv, read in order (one stream off the disc, engine/src/movie.c):
  sector 0: "SMGV", u32 rate, channels, frames, chunks, the biggest chunk's bytes, then each chunk's bytes;
  chunks, a second each (CHUNK frames): u32 video bytes, audio bytes, 8 zero; the video (an MPEG-2 elementary stream
  at 29.97 fps without B pictures for libmpeg, cut at a picture start code, zeros to 16 bytes: stuffing before the
  next start code), the sound of those frames (s16le, interleaved), zeros to the sector.

usage: python tools/movies.py [fs dir, default data/fs/files] [out dir, default out/packs] [name ...]
"""
import struct
import subprocess
import sys
from pathlib import Path

CHUNK = 30
RATE = 32000
SECTOR = 2048


def pad(b, n):
    return b + bytes(-len(b) % n)


def pack(m2v, pcm):
    pics = []
    i = m2v.find(b"\x00\x00\x01\x00")
    while i >= 0:
        pics.append(i)
        i = m2v.find(b"\x00\x00\x01\x00", i + 4)
    cuts = [0] + pics[CHUNK::CHUNK] + [len(m2v)]
    per = RATE * 4 * CHUNK * 1001 // 30000  # sound bytes of CHUNK frames at 30000 / 1001 fps (128128)
    chunks = []
    for k in range(len(cuts) - 1):
        v = pad(m2v[cuts[k]:cuts[k + 1]], 16)
        a = pcm[k * per:(k + 1) * per if k < len(cuts) - 2 else len(pcm)]
        chunks.append(pad(struct.pack("<II8x", len(v), len(a)) + v + a, SECTOR))
    head = b"SMGV" + struct.pack("<5I", RATE, 2, len(pics), len(chunks), max(map(len, chunks)))
    head += struct.pack(f"<{len(chunks)}I", *map(len, chunks))
    assert len(head) <= SECTOR, "too many chunks for the header sector"
    return pad(head, SECTOR) + b"".join(chunks), len(pics)


def convert(thp, out):
    out.mkdir(parents=True, exist_ok=True)
    v = out / (thp.stem + ".m2v")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(thp), "-an", "-vf", "fps=30000/1001",
                    "-c:v", "mpeg2video", "-b:v", "6M", "-maxrate", "8M", "-bufsize", "1835k", "-bf", "0", "-g", "15",
                    "-mbd", "rd", "-trellis", "2", "-cmp", "2", "-subcmp", "2", "-qmin", "1",
                    "-f", "mpeg2video", str(v)], check=True)
    pcm = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", str(thp), "-vn", "-ac", "2", "-ar", str(RATE),
                          "-f", "s16le", "-"], capture_output=True).stdout  # the last ADPCM packet is short: an error, kept
    smv, n = pack(v.read_bytes(), pcm)
    v.unlink()
    (out / (thp.stem + ".smv")).write_bytes(smv)
    return n, len(smv)


if __name__ == "__main__":
    fs = Path(sys.argv[1] if len(sys.argv) > 1 else "data/fs/files")
    out = Path(sys.argv[2] if len(sys.argv) > 2 else "out/packs") / "movies"
    names = sys.argv[3:]
    for thp in sorted((fs / "MovieData").glob("*.thp")):
        if names and thp.stem not in names:
            continue
        n, size = convert(thp, out)
        print(f"{thp.stem}: {n} frames, {size >> 20} MB")
