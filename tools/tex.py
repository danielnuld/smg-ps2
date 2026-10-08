"""J3D textures and materials -> PS2 textures for the slice blob (phase 2, task 2.1).

GX decoding: TEX1 holds 0x20-byte ResTIMG headers (format @0, w @2, h @4, wrapS @6, wrapT @7, image offset @0x1C,
from the header; budget.bti_headers). Formats are tiled (GX block sizes, GXTexture.c GX_TILE_SHIFT in budget.py):
I4 8x8, I8 8x4, IA4 8x4, IA8 4x4, RGB565 4x4, RGB5A3 4x4, RGBA8 4x4 (AR then GB halves), CMPR 8x8 of four 4x4 DXT1
sub-blocks. Palette formats (C4/C8/C14X2) are not decoded yet: none in Gateway 2; decode() raises on them.

PS2 rule (recorded in docs/phase2-report.md, same bytes as phase 0's lever, budget.py LEVERS):
  1. GX mirror wrap -> the texture is doubled with a mirrored copy on that axis (the GS has no mirror mode) and the
     UVs on that axis are halved; the axis then repeats.
  2. Each axis is halved (box filter) until it is <= 128.
  3. 16 colours (Pillow FASTOCTREE, alpha kept) -> PSMT4 + a 16-entry PSMCT32 CLUT, alpha scaled to the GS's 0..0x80.
  4. No mip levels. ponytail: distant textures shimmer; mips when VRAM and the upload time allow.
Alpha kind from the texture: 0 opaque, 1 cut-out (only 0 / 255), 2 blended.

Materials (J3DMaterialFactory.hpp J3DMaterialInitData; J3DModelLoader.hpp J3DMaterialBlock): shape -> material from
the INF1 hierarchy (0x01 open, 0x02 close, 0x11 material, 0x12 shape); material -> the first TEV stage with a texture
map -> TEX1 index, its tex coord source and its texture matrix. A texture matrix is applied when its type is plain SRT
(mInfo & 0x3F == 0: J3DGetTextureMtx / J3DGetTextureMtxMaya by mInfo bit 7, J3DTransform.cpp:190-245); projected
types (environment maps) keep the raw coordinates and are counted.
"""
import math
import struct

import numpy as np
from PIL import Image

from budget import u16, u32

MAX_DIM = 128
PSMT4 = 0x14


# ---------- GX decoding ----------
def _tiles(px, w, h, bw, bh):
    """Block-ordered pixels (nby * nbx * bh * bw, C) -> (h, w, C)."""
    nbx, nby = (w + bw - 1) // bw, (h + bh - 1) // bh
    a = px.reshape(nby, nbx, bh, bw, -1).transpose(0, 2, 1, 3, 4).reshape(nby * bh, nbx * bw, -1)
    return a[:h, :w]


def _rgb565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), np.full_like(v, 255)], -1)


def decode(data, fmt, w, h):
    """GX image (top level) -> uint8 RGBA (h, w, 4)."""
    nb = lambda bw, bh: ((w + bw - 1) // bw) * ((h + bh - 1) // bh) * bw * bh
    if fmt == 0:     # I4
        n = nb(8, 8)
        b = np.frombuffer(data, np.uint8, n // 2)
        i = np.stack([b >> 4, b & 15], -1).reshape(-1) * 17
        px = np.stack([i, i, i, np.full_like(i, 255)], -1)
        return _tiles(px.astype(np.uint8), w, h, 8, 8)
    if fmt == 1:     # I8
        i = np.frombuffer(data, np.uint8, nb(8, 4))
        return _tiles(np.stack([i, i, i, np.full_like(i, 255)], -1), w, h, 8, 4)
    if fmt == 2:     # IA4: alpha high nibble, intensity low
        b = np.frombuffer(data, np.uint8, nb(8, 4))
        i, a = (b & 15) * 17, (b >> 4) * 17
        return _tiles(np.stack([i, i, i, a], -1).astype(np.uint8), w, h, 8, 4)
    if fmt == 3:     # IA8: alpha byte, then intensity
        b = np.frombuffer(data, np.uint8, nb(4, 4) * 2).reshape(-1, 2)
        a, i = b[:, 0], b[:, 1]
        return _tiles(np.stack([i, i, i, a], -1), w, h, 4, 4)
    if fmt == 4:     # RGB565
        v = np.frombuffer(data, ">u2", nb(4, 4)).astype(np.uint32)
        return _tiles(_rgb565(v).astype(np.uint8), w, h, 4, 4)
    if fmt == 5:     # RGB5A3: top bit set = RGB555, else ARGB3444
        v = np.frombuffer(data, ">u2", nb(4, 4)).astype(np.uint32)
        op = v >> 15 == 1
        c5 = lambda x: (x << 3) | (x >> 2)
        c4 = lambda x: x * 17
        r = np.where(op, c5((v >> 10) & 31), c4((v >> 8) & 15))
        g = np.where(op, c5((v >> 5) & 31), c4((v >> 4) & 15))
        bl = np.where(op, c5(v & 31), c4(v & 15))
        a = np.where(op, 255, ((v >> 12) & 7) * 255 // 7)
        return _tiles(np.stack([r, g, bl, a], -1).astype(np.uint8), w, h, 4, 4)
    if fmt == 6:     # RGBA8: per 4x4 block 16 AR pairs then 16 GB pairs
        n = nb(4, 4) // 16
        b = np.frombuffer(data, np.uint8, n * 64).reshape(n, 2, 16, 2)
        px = np.stack([b[:, 0, :, 1], b[:, 1, :, 0], b[:, 1, :, 1], b[:, 0, :, 0]], -1).reshape(-1, 4)
        return _tiles(px, w, h, 4, 4)
    if fmt == 14:    # CMPR: 8x8 blocks of 2x2 DXT1 sub-blocks (8 bytes: c0, c1 BE565, 16 2-bit indices MSB first)
        n = nb(8, 8) // 16
        sb = np.frombuffer(data, np.uint8, n * 8).reshape(-1, 8)
        c0 = sb[:, 0].astype(np.uint32) << 8 | sb[:, 1]
        c1 = sb[:, 2].astype(np.uint32) << 8 | sb[:, 3]
        p0, p1 = _rgb565(c0).astype(np.int32), _rgb565(c1).astype(np.int32)
        four = (c0 > c1)[:, None]
        p2 = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2)
        p3 = np.where(four, (p0 + 2 * p1) // 3, 0)
        pal = np.stack([p0, p1, p2, p3], 1)                                     # (N, 4, 4)
        idx = (sb[:, 4:, None] >> np.array([6, 4, 2, 0], np.uint8)) & 3        # (N, 4 rows, 4 cols)
        px = np.take_along_axis(pal, idx.reshape(-1, 16)[:, :, None].astype(np.int64), 1)  # (N, 16, 4)
        # sub-blocks in Z order inside each 8x8 block -> 8x8 block-ordered pixels
        px = px.reshape(-1, 2, 2, 4, 4, 4).transpose(0, 1, 3, 2, 4, 5).reshape(-1, 4)
        return _tiles(px.astype(np.uint8), w, h, 8, 8)
    raise NotImplementedError(f"GX texture format {fmt}")


def textures(b, tex1):
    """TEX1 block at tex1 -> [dict(fmt, w, h, wrap_s, wrap_t, rgba)]."""
    out = []
    base = tex1 + u32(b, tex1 + 0x0C)
    for i in range(u16(b, tex1 + 8)):
        hd = base + i * 0x20
        fmt, w, h = b[hd], u16(b, hd + 2), u16(b, hd + 4)
        out.append(dict(fmt=fmt, w=w, h=h, wrap_s=b[hd + 6], wrap_t=b[hd + 7],
                        rgba=decode(b[hd + u32(b, hd + 0x1C):], fmt, w, h)))
    return out


# ---------- PS2 rule ----------
def to_ps2(rgba, wrap_s, wrap_t):
    """-> dict(w, h, psm, clamp_s, clamp_t, alpha, pix (PSMT4 bytes), clut (16 x u32 RGBA, a 0..0x80), u_scale,
    v_scale): steps 1-3 of the module rule."""
    us = vs = 1.0
    if wrap_s == 2:
        rgba, us = np.concatenate([rgba, rgba[:, ::-1]], 1), 0.5
    if wrap_t == 2:
        rgba, vs = np.concatenate([rgba, rgba[::-1]], 0), 0.5
    h, w = rgba.shape[:2]
    img = Image.fromarray(np.ascontiguousarray(rgba), "RGBA")
    if w & (w - 1) or h & (h - 1):  # GX takes any size, the GS powers of two: nearest one (UVs are normalized)
        p2 = lambda n: min((1 << k for k in range(12)), key=lambda v: (abs(v - n), -v))
        w, h = p2(w), p2(h)
        img = img.resize((w, h), Image.Resampling.BOX)
    while w > MAX_DIM or h > MAX_DIM:
        w, h = (w // 2 if w > MAX_DIM else w), (h // 2 if h > MAX_DIM else h)
        img = img.resize((w, h), Image.Resampling.BOX)
    a = np.asarray(img)[:, :, 3]
    alpha = 0 if a.min() == 255 else 1 if np.isin(a, (0, 255)).all() else 2
    q = img.quantize(16, method=Image.Quantize.FASTOCTREE)
    idx = np.asarray(q, np.uint8)
    pal = np.array((q.getpalette(rawmode="RGBA") or [])[:64] + [0] * 64, np.uint32)[:64].reshape(16, 4)
    if alpha == 0:
        pal[:, 3] = 255
    pal[:, 3] = (pal[:, 3] * 128 + 127) // 255
    clut = (pal[:, 0] | pal[:, 1] << 8 | pal[:, 2] << 16 | pal[:, 3] << 24).astype("<u4").tobytes()
    pix = (idx[:, 0::2] | idx[:, 1::2] << 4).astype(np.uint8).tobytes() if w > 1 else idx.tobytes()
    return dict(w=w, h=h, psm=PSMT4, clamp_s=0 if wrap_s else 1, clamp_t=0 if wrap_t else 1, alpha=alpha, pix=pix,
                clut=clut, u_scale=us, v_scale=vs)


# ---------- materials ----------
def blocks(b):
    o, out = 0x20, {}
    for _ in range(u32(b, 0x0C)):
        out[b[o:o + 4]] = o
        o += u32(b, o + 4)
    return out


def shape_materials(b, inf1):
    """INF1 hierarchy -> {shape: material}."""
    p, cur, stack, out = inf1 + u32(b, inf1 + 0x14), None, [], {}
    while True:
        t, i = u16(b, p), u16(b, p + 2)
        if t == 0:
            return out
        if t == 0x01:
            stack.append(cur)
        elif t == 0x02:
            cur = stack.pop() if stack else None
        elif t == 0x11:
            cur = i
        elif t == 0x12:
            out[i] = cur
        p += 4


def tex_mtx(b, e):
    """J3DTexMtxInfo at e -> 2x3 rows for (s, t, 1), or None for projected types."""
    info = b[e + 1]
    if info & 0x3F:
        return None
    cx, cy = struct.unpack_from(">2f", b, e + 4)[:2]
    sx, sy = struct.unpack_from(">2f", b, e + 0x10)
    rot = struct.unpack_from(">h", b, e + 0x18)[0] * math.pi / 0x8000
    tx, ty = struct.unpack_from(">2f", b, e + 0x1C)
    sr, cr = math.sin(rot), math.cos(rot)
    if info >> 7:   # J3DGetTextureMtxMaya
        return ((sx * cr, sy * sr, (tx - 0.5) * cr - sr * (ty - 0.5 + sy) + 0.5),
                (-sx * sr, sy * cr, -(tx - 0.5) * sr - cr * (ty - 0.5 + sy) + 0.5))
    return ((sx * cr, -sx * sr, -sx * cr * cx + sx * sr * cy + cx + tx),   # J3DGetTextureMtx
            (sy * sr, sy * cr, -sy * sr * cx - sy * cr * cy + cy + ty))


def material_tex(b, mat3, m, tex1=None):
    """Material m -> dict(tex = TEX1 index, attr = VTX1 attribute of its coords (13 = TEX0), mtx = 2x3 or None,
    projected = bool), or None when no TEV stage samples a texture. With tex1 (TEX1 block offset) a colour texture wins
    over a greyscale one (I4 / I8 / IA4 / IA8: shading ramps the TEV multiplies in, e.g. Mario's hair stage 0 is a
    white IA8 ramp the size of the brown colour map), then the largest area (first on ties). ponytail: one texture per
    material; the other stages are dropped."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    stages = b[off(0x58) + b[e + 4]]
    best, area = None, -1
    for k in range(stages):
        oi = u16(b, e + 0xBC + 2 * k)
        if oi == 0xFFFF:
            continue
        coord, tmap = b[off(0x4C) + 4 * oi], b[off(0x4C) + 4 * oi + 1]
        if tmap == 0xFF:
            continue
        tex = u16(b, off(0x48) + 2 * u16(b, e + 0x84 + 2 * tmap))
        a = 0
        if tex1 is not None:
            hd = tex1 + u32(b, tex1 + 0x0C) + 0x20 * tex
            a = u16(b, hd + 2) * u16(b, hd + 4) + (1 << 24 if b[hd] > 3 else 0)  # colour formats first
        if a <= area:
            continue
        ci = u16(b, e + 0x28 + 2 * coord)
        src, mtx = b[off(0x38) + 4 * ci + 1], b[off(0x38) + 4 * ci + 2]
        res = dict(tex=tex, attr=13 + src - 4 if 4 <= src <= 11 else None, mtx=None, projected=False)
        if mtx != 60:
            mi = u16(b, e + 0x48 + 2 * ((mtx - 30) // 3))
            if mi != 0xFFFF:
                res["mtx"] = tex_mtx(b, off(0x40) + 0x64 * mi)
                res["projected"] = res["mtx"] is None
        best, area = res, a
        if tex1 is None:
            break
    return best


def material_tint(b, mat3, m, tex1, tex):
    """(dark, bright) RGB a greyscale texture is coloured with, or None. SMG often lerps from konst colour 0 to TEV
    colour 0 by a greyscale texture's intensity (SummerSky: blue (0, 35, 157) to white clouds). ponytail: that one
    TEV pattern assumed whenever the texture is I4 / I8 / IA4 / IA8 and konst 0 is not white; no TEV evaluation."""
    if tex1 is None or b[tex1 + u32(b, tex1 + 0x0C) + 0x20 * tex] > 3:
        return None
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    k0, c0 = u16(b, e + 0x94), u16(b, e + 0xDC)
    if k0 == 0xFFFF:
        return None
    dark = tuple(b[off(0x54) + 4 * k0 + i] for i in range(3))
    if dark == (255, 255, 255):
        return None
    bright = (255, 255, 255)
    if c0 != 0xFFFF:
        bright = tuple(max(0, min(255, v)) for v in struct.unpack_from(">3h", b, off(0x50) + 8 * c0))
    return dark, bright


def tint(rgba, dark, bright):
    """greyscale RGBA -> dark + (bright - dark) * intensity, alpha kept"""
    out = rgba.copy()
    i = rgba[..., 0:1].astype(np.float32) / 255.0
    out[..., :3] = (np.array(dark, np.float32) + (np.array(bright, np.float32) - np.array(dark, np.float32)) * i).astype(np.uint8)
    return out


def material_blend(b, mat3, m):
    """Material m -> (kind, z_write, alpha): kind 0 opaque, 1 blended (Cs - Cd) As + Cd, 2 additive (destination
    factor ONE); z_write from its z mode; alpha = material colour 0's alpha (0..255), the constant when no texture
    alpha. MAT3 entry: z mode index u8 at 6, material colour indices u16 at 8, blend mode index u16 at 0x148; header
    tables: material colours 0x20, blend modes 0x70 ({type, src, dst, logic op}), z modes 0x74 ({on, func, update})."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    bm = off(0x70) + 4 * u16(b, e + 0x148)
    kind = 0
    if b[bm] == 1:  # GX_BM_BLEND
        kind = 2 if b[bm + 2] == 1 else 1  # dst GX_BL_ONE: additive
    zw = b[off(0x74) + 4 * b[e + 6] + 2]
    mc = u16(b, e + 8)
    alpha = b[off(0x20) + 4 * mc + 3] if mc != 0xFFFF else 255
    return kind, zw, alpha


def selftest():
    # RGB565 4x4 block: all red -> (255, 0, 0, 255)
    assert decode(b"\xf8\x00" * 16, 4, 4, 4)[1, 2].tolist() == [255, 0, 0, 255]
    # I8 8x4 block of 0..31 in block order -> row 1, col 0 is byte 8
    assert decode(bytes(range(32)), 1, 8, 4)[1, 0, 0] == 8
    # CMPR: one 8x8 block; sub-block 0 (top-left) c0 white > c1 black, indices 0 everywhere -> white;
    # sub-block 1 (top-right) all index 1 -> black; sub-block 2 (bottom-left) c0 <= c1, index 3 -> transparent
    sb0 = b"\xff\xff\x00\x00" + b"\x00" * 4
    sb1 = b"\xff\xff\x00\x00" + b"\x55" * 4
    sb2 = b"\x00\x00\xff\xff" + b"\xff" * 4
    img = decode(sb0 + sb1 + sb2 + sb0, 14, 8, 8)
    assert img[0, 0].tolist() == [255, 255, 255, 255] and img[0, 7].tolist() == [0, 0, 0, 255], img[0]
    assert img[7, 0, 3] == 0 and img[7, 7].tolist() == [255, 255, 255, 255]
    # PS2: mirror doubles the axis and halves the UVs; 2 colours stay exact; nibble order low = left pixel
    t = np.zeros((4, 4, 4), np.uint8)
    t[..., 3] = 255
    t[:, :2, 0] = 255
    p = to_ps2(t, 2, 1)
    assert (p["w"], p["h"], p["u_scale"], p["clamp_s"], p["alpha"]) == (8, 4, 0.5, 0, 0), p
    clut = np.frombuffer(p["clut"], "<u4")
    first = p["pix"][0]
    assert clut[first & 15] & 0xFF == 255 and clut[first >> 4] & 0xFF == 255      # pixels 0, 1 red
    assert clut[p["pix"][1] & 15] & 0xFF == 0                                        # pixel 2 black
    assert clut[p["pix"][3] >> 4] & 0xFF == 255                                      # pixel 7 red (mirrored)
    assert (clut >> 24).max() == 0x80
    # Maya SRT with scale 2, no rotation/translation: s' = 2 s, t' = 2 t - 1 (J3DGetTextureMtxMaya)
    e = bytearray(0x64)
    e[1] = 0x80
    struct.pack_into(">2f", e, 0x10, 2.0, 2.0)
    m = tex_mtx(bytes(e), 0)
    assert abs(m[0][0] - 2) < 1e-6 and abs(m[0][2]) < 1e-6 and abs(m[1][1] - 2) < 1e-6 and abs(m[1][2] + 1) < 1e-6, m
    print("selftest ok")


if __name__ == "__main__":
    selftest()
