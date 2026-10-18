"""The memory card save's icon for the PS2 browser: engine/src/save.icn and engine/src/icon.sys (mc.c writes both
into mc0:/SMGPS2/, assets.S links them in).

The picture is the Wii save's own icon (SaveDataBannerCreator::setupBannerInfo: SaveIconBanner.arc SaveIcon00.bti,
48 x 48) scaled to the 128 x 128 a PS2 icon texture has, on a flat quad.
ponytail: one flat textured quad, no 3D model / animation (the Wii icon is a flat picture too).

.icn (uncompressed): u32 0x10000, u32 shapes 1, u32 texture type 7, f32 1.0, u32 vertices; per vertex s16 x, y, z, w
(4096 = 1.0, y down), s16 normal x, y, z, w, s16 u, v (4096 = 1.0, v 0 at the top), u8 r, g, b, a; the animation
(u32 1, frame length 1, f32 speed, offset 0, frames 1; frame: shape 0, keys 1, key (0.0, 1.0)); 128 x 128 RGB5A1.
icon.sys: libmc.h mcIcon (964 bytes), the title in Shift-JIS (full-width ASCII).
usage: python tools/ps2icon.py data/fs/files"""
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import budget as B
import lyt

TITLE = ("Super Mario ", "Galaxy")  # TITLE_MAIN, two lines


def texture(fsp):
    arc = dict(B.rarc_files((Path(fsp) / "ObjectData/SaveIconBanner.arc").read_bytes()))
    im = np.asarray(Image.fromarray(lyt.bti(arc["saveicon00.bti"])).resize((128, 128), Image.LANCZOS), np.uint32)
    r, g, b, a = (im[..., i] >> 3 for i in range(4))
    px = r | g << 5 | b << 10 | (a >> 4) << 15  # A: alpha >= 128
    return px.astype("<u2").tobytes()


def icn(tex):
    s, top, bot = 7000, -14500, -500  # about the size of a browser icon (OPL's: x +-10230, y -12700..-7830)
    quad = [(-s, top, 0, 0), (s, top, 4096, 0), (-s, bot, 0, 4096), (s, top, 4096, 0), (s, bot, 4096, 4096),
            (-s, bot, 0, 4096)]
    out = struct.pack("<IIIfI", 0x10000, 1, 7, 1.0, len(quad))
    for x, y, u, v in quad:
        out += struct.pack("<4h4h2h4B", x, y, 0, 0x400, 0, 0, -4096, 0, u, v, 0x80, 0x80, 0x80, 0x80)
    out += struct.pack("<IIfII", 1, 1, 1.0, 0, 1) + struct.pack("<IIff", 0, 1, 0.0, 1.0)
    return out + tex


def sjis(s):
    return "".join("　" if c == " " else chr(ord(c) + 0xFEE0) for c in s).encode("shift_jis")


def icon_sys(name):
    title = sjis(TITLE[0]) + sjis(TITLE[1])
    out = b"PS2D" + struct.pack("<HHII", 0, len(sjis(TITLE[0])), 0, 0x60)
    out += struct.pack("<16I", *[0x00, 0x20, 0x60, 0] * 2 + [0x10, 0x10, 0x40, 0] * 2)  # bgCol: dark blue, top to bottom
    out += struct.pack("<12f", 0.5, 0.5, 0.5, 0, 0, -0.4, -0.1, 0, -0.5, -0.5, 0.5, 0)  # lightDir
    out += struct.pack("<12f", 0.3, 0.3, 0.3, 0, 0.2, 0.2, 0.2, 0, 0.1, 0.1, 0.1, 0)  # lightCol
    out += struct.pack("<4f", 0.7, 0.7, 0.7, 0)  # lightAmbient: the flat picture mostly unlit
    out += title.ljust(68, b"\0") + name.ljust(64, b"\0") * 3 + bytes(512)
    assert len(out) == 964 and len(title) <= 68
    return out


if __name__ == "__main__":
    d = Path(__file__).resolve().parent.parent / "engine/src"
    data = icn(texture(sys.argv[1]))
    assert len(data) == 20 + 6 * 24 + 20 + 16 + 128 * 128 * 2
    (d / "save.icn").write_bytes(data)
    (d / "icon.sys").write_bytes(icon_sys(b"save.icn"))
    print("save.icn", len(data), "icon.sys 964")
