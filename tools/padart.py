"""The control guidance pictures redrawn for the PS2 pad (user decision 2026-11-20): the Wii layouts SpinGuidance,
BallGuidance and TiltGuidance show a hand holding a Wii Remote (128 x 128, 15 pictures); here each is a DualShock
with the control the port reads for that action lit. lyt.py asks picture(<texture name>) before it decodes a TPL.

  Spin (PlayerActionGuidance, wait: 7 8 9 8 7): the swing is SQUARE -> idle, lit, pressed
  Ball (Tamakoro: the left stick rolls it, CROSS jumps): 5 / 4 / 8 stick centred, 1 left, 2 right, 3 up, 7 down
        (the Wii's "raise the remote" 7 -> 8: the stick let go), 6 CROSS pressed
  Tilt (SurfRay: the left stick): 1 idle, 2 centred, 3 left, 4 right

Drawn with signed distances at 4x and averaged down: `python tools/padart.py out.png` writes a contact sheet.
"""
import struct
import sys
import zlib

import numpy as np

N, SS = 128, 4
BG = (170, 214, 255)
INK = (45, 50, 60)
BODY = (238, 240, 244)
DIM = (150, 156, 168)
LIT = (255, 96, 64)
SQUARE = (232, 120, 196)
CROSS = (104, 150, 240)
BURST = (255, 236, 110)


def _grid():
    c = (np.arange(N * SS) + 0.5) / SS
    return np.meshgrid(c, c)


def _circle(x, y, cx, cy, r):
    return np.hypot(x - cx, y - cy) - r


def _box(x, y, cx, cy, hw, hh, r=0.0):
    dx, dy = np.abs(x - cx) - (hw - r), np.abs(y - cy) - (hh - r)
    return np.hypot(np.maximum(dx, 0), np.maximum(dy, 0)) + np.minimum(np.maximum(dx, dy), 0) - r


def _seg(x, y, ax, ay, bx, by, w):
    px, py, vx, vy = x - ax, y - ay, bx - ax, by - ay
    t = np.clip((px * vx + py * vy) / (vx * vx + vy * vy), 0, 1)
    return np.hypot(px - vx * t, py - vy * t) - w


def _paint(img, sd, col):
    a = np.clip(0.5 - sd * SS, 0, 1)[..., None]
    img[:] = img * (1 - a) + np.array(col, np.float32) * a


def draw(stick=None, button=None, state=0):
    """stick: None (not the subject) or (dx, dy) in -1..1 (screen y down); button: None, 'square' or 'cross';
    state: 0 idle, 1 lit, 2 pressed (a burst round it)."""
    x, y = _grid()
    img = np.empty((N * SS, N * SS, 3), np.float32)
    img[:] = BG
    pad = np.minimum(_box(x, y, 64, 60, 44, 19, 15), np.minimum(_circle(x, y, 29, 82, 15), _circle(x, y, 99, 82, 15)))
    pad = np.minimum(pad, np.minimum(_box(x, y, 27, 74, 13, 12, 6), _box(x, y, 101, 74, 13, 12, 6)))
    _paint(img, pad - 1.6, INK)
    _paint(img, pad, BODY)
    _paint(img, np.minimum(_box(x, y, 36, 56, 9, 3.2, 1), _box(x, y, 36, 56, 3.2, 9, 1)), DIM)  # the direction keys
    for name, (ox, oy) in (("triangle", (0, -9)), ("circle", (9, 0)), ("cross", (0, 9)), ("square", (-9, 0))):
        cx, cy = 92 + ox, 56 + oy
        on = button == name and state > 0
        if on and state == 2:
            for k in range(8):
                a = k * np.pi / 4 + np.pi / 8
                _paint(img, _seg(x, y, cx + 10 * np.cos(a), cy + 10 * np.sin(a), cx + 15 * np.cos(a), cy + 15 * np.sin(a), 1.3), BURST)
        r = 6.5 if on else 4.2
        _paint(img, _circle(x, y, cx, cy, r + 1.2), INK)
        _paint(img, _circle(x, y, cx, cy, r), (SQUARE if name == "square" else CROSS) if on else DIM)
        if on and name == "square":
            _paint(img, np.abs(_box(x, y, cx, cy, 3, 3)) - 0.8, (255, 255, 255))
        if on and name == "cross":
            _paint(img, np.minimum(_seg(x, y, cx - 3, cy - 3, cx + 3, cy + 3, 0.8), _seg(x, y, cx - 3, cy + 3, cx + 3, cy - 3, 0.8)), (255, 255, 255))
    for i, bx in enumerate((50, 78)):  # the sticks: the left one is the subject
        _paint(img, _circle(x, y, bx, 74, 9.5), INK)
        _paint(img, _circle(x, y, bx, 74, 8.2), DIM)
        dx, dy = stick if (stick is not None and i == 0) else (0.0, 0.0)
        cx, cy = bx + 5 * dx, 74 + 5 * dy
        _paint(img, _circle(x, y, cx, cy, 6.6), INK)
        _paint(img, _circle(x, y, cx, cy, 5.4), LIT if (stick is not None and i == 0) else BODY)
    if stick is not None and (stick[0] or stick[1]):  # an arrow the way it is pushed, over both sticks
        dx, dy = stick
        tx, ty = 50 + 21 * dx, 74 + 21 * dy
        sd = _seg(x, y, 50 + 12 * dx, 74 + 12 * dy, tx, ty, 1.6)
        for s in (1, -1):
            sd = np.minimum(sd, _seg(x, y, tx, ty, tx - 5 * dx + s * 4 * dy, ty - 5 * dy - s * 4 * dx, 1.6))
        _paint(img, sd - 1.2, INK)
        _paint(img, sd, LIT)
    out = img.reshape(N, SS, N, SS, 3).mean((1, 3))
    return np.dstack([np.clip(out + 0.5, 0, 255).astype(np.uint8), np.full((N, N), 255, np.uint8)])


PICTURES = {
    "imspinguidance7new": dict(button="square", state=0),
    "imspinguidance8new": dict(button="square", state=1),
    "imspinguidance9new": dict(button="square", state=2),
    "imballguidance1new": dict(stick=(-1, 0)),
    "imballguidance2new": dict(stick=(1, 0)),
    "imballguidance3new": dict(stick=(0, -1)),
    "imballguidance4new": dict(stick=(0, 0), button="cross", state=1),
    "imballguidance5new": dict(stick=(0, 0)),
    "imballguidance6new": dict(stick=(0, 0), button="cross", state=2),
    "imballguidance7new": dict(stick=(0, 1)),
    "imballguidance8new": dict(stick=(0, 0)),
    "mytiltguidance1new": dict(),
    "mytiltguidance2new": dict(stick=(0, 0)),
    "mytiltguidance3new": dict(stick=(-1, 0)),
    "mytiltguidance4new": dict(stick=(1, 0)),
}


def picture(texname):
    """RGBA (128, 128, 4) for a guidance texture's name (any case, with or without .tpl), None for any other."""
    key = texname.lower().rsplit("/", 1)[-1]
    key = key[:-4] if key.endswith(".tpl") else key
    return draw(**PICTURES[key]) if key in PICTURES else None


def _png(path, a):
    h, w = a.shape[:2]
    raw = b"".join(b"\0" + a[r].tobytes() for r in range(h))
    ch = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))


if __name__ == "__main__":
    names = sorted(PICTURES)
    assert picture("ImSpinGuidance9New.tpl").shape == (N, N, 4) and picture("ImBase.tpl") is None
    assert not np.array_equal(picture(names[0]), picture(names[1]))
    sheet = np.full((3 * (N + 4), 5 * (N + 4), 4), 255, np.uint8)
    sheet[..., :3] = 60
    for i, n in enumerate(names):
        r, c = (i // 5) * (N + 4) + 2, (i % 5) * (N + 4) + 2
        sheet[r:r + N, c:c + N] = picture(n)
        print(i, n)
    if len(sys.argv) > 1:
        _png(sys.argv[1], np.repeat(np.repeat(sheet, 2, 0), 2, 1))
