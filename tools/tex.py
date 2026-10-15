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
PSMT8 = 0x13


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
def to_ps2(rgba, wrap_s, wrap_t, hi=False):
    """-> dict(w, h, psm, clamp_s, clamp_t, alpha, pix (PSMT4 bytes), clut (16 x u32 RGBA, a 0..0x80), u_scale,
    v_scale): steps 1-3 of the module rule. hi (skies): PSMT8, 256 colours (CLUT in CSM1 order: index bits 3 and 4
    swapped), up to 2 * MAX_DIM, so star fields keep their points."""
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
    md = MAX_DIM * 2 if hi else MAX_DIM
    while w > md or h > md:
        w, h = (w // 2 if w > md else w), (h // 2 if h > md else h)
        img = img.resize((w, h), Image.Resampling.BOX)
    a = np.asarray(img)[:, :, 3]
    alpha = 0 if a.min() == 255 else 1 if np.isin(a, (0, 255)).all() else 2
    nc = 256 if hi else 16
    if hi and alpha == 0:  # opaque: median cut on RGB (fast octree keeps few levels of a smooth gradient)
        q = img.convert("RGB").quantize(nc, method=Image.Quantize.MEDIANCUT)
        rgb = q.getpalette() or []
        pal = np.zeros((nc, 4), np.uint32)
        pal[:len(rgb) // 3, :3] = np.array(rgb, np.uint32).reshape(-1, 3)[:nc]
    elif hi:
        q = img.quantize(nc, method=Image.Quantize.FASTOCTREE)
    else:
        # PSMT4: the base and its mips (levels 1-3 down to 8 texels, GS MIPTBP1, box-filtered) share one palette,
        # quantized from all of them (level k weighted 2^k) so the mips keep the in-between tones they average to
        levels = [img]
        while len(levels) < 4 and min(w, h) >> len(levels) >= 8:
            levels.append(levels[-1].resize((levels[-1].width // 2, levels[-1].height // 2), Image.Resampling.BOX))
        px = [np.asarray(lv).reshape(-1, 4) for lv in levels]
        allpx = np.concatenate([np.tile(p, (1 << k, 1)) for k, p in enumerate(px)])
        q = Image.fromarray(allpx.reshape(1, -1, 4), "RGBA").quantize(nc, method=Image.Quantize.FASTOCTREE)
    if not hi or alpha:
        pal = np.array((q.getpalette(rawmode="RGBA") or [])[:4 * nc] + [0] * 4 * nc, np.uint32)[:4 * nc].reshape(nc, 4)
    idx = np.asarray(q, np.uint8)
    if alpha == 0:
        pal[:, 3] = 255
    mips = []
    if not hi:
        flat, o = idx.reshape(-1), 0
        for k, (lv, p) in enumerate(zip(levels, px)):
            li = flat[o:o + len(p)].reshape(lv.height, lv.width)
            o += len(p) << k
            if k:
                mips.append((li[:, 0::2] | li[:, 1::2] << 4).astype(np.uint8).tobytes())
            else:
                idx = li
    pal[:, 3] = (pal[:, 3] * 128 + 127) // 255
    clut = (pal[:, 0] | pal[:, 1] << 8 | pal[:, 2] << 16 | pal[:, 3] << 24).astype("<u4")
    if hi:
        clut = clut[[i ^ ((i >> 3 ^ i >> 4) & 1) * 0x18 for i in range(256)]]  # CSM1: entries 8-15 <-> 16-23 per 32
        return dict(w=w, h=h, psm=PSMT8, clamp_s=0 if wrap_s else 1, clamp_t=0 if wrap_t else 1, alpha=alpha,
                    pix=idx.tobytes(), clut=clut.tobytes(), u_scale=us, v_scale=vs)
    clut = clut.tobytes()
    pix = (idx[:, 0::2] | idx[:, 1::2] << 4).astype(np.uint8).tobytes() if w > 1 else idx.tobytes()
    return dict(w=w, h=h, psm=PSMT4, clamp_s=0 if wrap_s else 1, clamp_t=0 if wrap_t else 1, alpha=alpha, pix=pix,
                clut=clut, u_scale=us, v_scale=vs, mips=mips)


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
        res = dict(tex=tex, attr=13 + src - 4 if 4 <= src <= 11 else None, mtx=None, projected=False, slot=None)
        if mtx != 60:
            res["slot"] = (mtx - 30) // 3  # the material's texture matrix a BTK names (texMtxIndex)
            mi = u16(b, e + 0x48 + 2 * ((mtx - 30) // 3))
            if mi != 0xFFFF:
                res["mtx"] = tex_mtx(b, off(0x40) + 0x64 * mi)
                res["maya"] = b[off(0x40) + 0x64 * mi + 1] >> 7
                res["projected"] = res["mtx"] is None
                if src == 1:  # GX_TG_NRM: an environment map (env_mtx)
                    res["env"] = env_mtx(b, off(0x40) + 0x64 * mi, b[off(0x38) + 4 * ci])
        best, area = res, a
        if tex1 is None:
            break
    return best


def env_layer(b, mat3, m, tex1, texs, over=None):
    """Material m's last TEV stage when it adds an environment map over the rest: PREV + K x TEXC (a ZERO, d CPREV,
    no bias / scale, into PREV) with its texture on a normal texgen (env_mtx) of another coordinate than stage 0's ->
    dict(coord, tex (TEX1 index), k (RGB: the TEV with that coordinate read as 1 minus as 0), env), else None. Its
    layer is drawn as a second, additive pass (tools/slice.py); the base is baked with it at 0 (zero_coord)."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    ns = b[off(0x58) + b[e + 4]]
    if ns < 2:
        return None
    oi, oi0 = u16(b, e + 0xBC + 2 * (ns - 1)), u16(b, e + 0xBC)
    if oi == 0xFFFF or oi0 == 0xFFFF:
        return None
    coord, tmap = b[off(0x4C) + 4 * oi], b[off(0x4C) + 4 * oi + 1]
    if tmap == 0xFF or coord == b[off(0x4C) + 4 * oi0]:
        return None
    st = b[off(0x5C) + 20 * u16(b, e + 0xE4 + 2 * (ns - 1)):][:20]
    if not (st[1] == 15 and st[4] == 0 and st[5] == 0 and st[6] == 0 and st[7] == 0 and (st[2] == 8 or st[3] == 8)
            and st[9] & 3 == 0):
        return None
    ci = u16(b, e + 0x28 + 2 * coord)
    if ci == 0xFFFF or b[off(0x38) + 4 * ci + 1] != 1:
        return None
    mtx = b[off(0x38) + 4 * ci + 2]
    mi = u16(b, e + 0x48 + 2 * ((mtx - 30) // 3)) if mtx != 60 else 0xFFFF
    env = env_mtx(b, off(0x40) + 0x64 * mi, b[off(0x38) + 4 * ci]) if mi != 0xFFFF else None
    if env is None:
        return None
    mn = material_name(b, mat3, m)
    zero = dict(over or {})
    zero[mn] = dict(zero.get(mn, {}), zero_coord=coord)
    k = np.clip(tev(b, mat3, m, tex1, texs, None, over)[:3] - tev(b, mat3, m, tex1, texs, None, zero)[:3], 0, 4)
    t = u16(b, off(0x48) + 2 * u16(b, e + 0x84 + 2 * tmap))
    return dict(coord=coord, tex=t, k=k, env=env, name=mn)


def env_mtx(b, t, typ):
    """A normal texgen's matrix (J3DTexMtx::calcTexMtx, modes 6 and 7 without Maya SRT) on the view-space normal (GX:
    x right, y up, z to the viewer) -> 12 floats, columns n.x, n.y, n.z, 1 of rows s, t, q. Mode 6:
    J3DGetTextureMtxOld x qMtx2 (0.5, -0.5 and +0.5); mode 7: J3DGetTextureMtx (translation on z) x qMtx (projective).
    typ 1 (GX_TG_MTX2x4) leaves q = 1, typ 0 (MTX3x4) keeps the matrix's third row. None for the other modes."""
    info = b[t + 1]
    mode = info & 0x3F
    if mode not in (6, 7) or info >> 7:
        return None
    cxc, cyc, _ = struct.unpack_from(">3f", b, t + 4)
    sx_, sy_ = struct.unpack_from(">2f", b, t + 0x10)
    rot = struct.unpack_from(">h", b, t + 0x18)[0] * math.pi / 32768
    tx, ty = struct.unpack_from(">2f", b, t + 0x1C)
    cx, sx, sy, cy = sx_ * math.cos(rot), sx_ * math.sin(rot), sy_ * math.sin(rot), sy_ * math.cos(rot)
    t0 = -cx * cxc + sx * cyc + cxc + tx
    t1 = -sy * cxc - cy * cyc + cyc + ty
    col_x = [0.5 * cx, 0.5 * sy, 0.0]
    col_y = [0.5 * sx, -0.5 * cy, 0.0]
    if mode == 6:
        col_z, col_c = [0.0, 0.0, 1.0], [0.5 * cx - 0.5 * sx + t0, 0.5 * sy + 0.5 * cy + t1, 0.0]
    else:
        col_z, col_c = [0.5 * cx - 0.5 * sx + t0, 0.5 * sy + 0.5 * cy + t1, 1.0], [0.0, 0.0, 0.0]
    if typ != 0:  # MTX2x4: q = 1
        col_x[2], col_y[2], col_z[2], col_c[2] = 0.0, 0.0, 0.0, 1.0
    return col_x + col_y + col_z + col_c


KSEL_FRAC = [1.0, 7 / 8, 6 / 8, 5 / 8, 4 / 8, 3 / 8, 2 / 8, 1 / 8]


def names(b, off):
    """A J3D string table at `off` -> its names in order."""
    n = u16(b, off)
    out = []
    for k in range(n):
        o = u16(b, off + 4 + 4 * k + 2)
        e = b.index(b"\0", off + o)
        out.append(b[off + o:e].decode("latin-1"))
    return out


def material_name(b, mat3, m):
    """MAT3 material m's name (its name table at 0x14)."""
    return names(b, mat3 + u32(b, mat3 + 0x14))[m]


def brk(b, frame):
    """A J3D TEV register animation (J3D1brk1, TRK1) at `frame` -> {material name: {"reg": {id: rgba}, "konst":
    {id: rgba}}}, rgba floats 0..1 (register values are s16 / 255). Keys: count 1 a constant, else (time, value,
    tangent) triplets (tangent type 0) or (time, value, in, out) quads, Hermite between them."""
    t = b.find(b"TRK1")
    if b[:8] != b"J3D1brk1" or t < 0:
        return {}
    h = lambda k: u16(b, t + k)
    w = lambda k: u32(b, t + k)
    out = {}

    def herm(f, t0, v0, m0, t1, v1, m1):
        d = t1 - t0
        x = (f - t0) / d if d else 0.0
        x2 = x * x
        return v0 + x * d * m0 + x2 * (3 * (v1 - v0) - 2 * d * m0 - d * m1) + x2 * x * (2 * (v0 - v1) + d * m0 + d * m1)

    def chan(vals_off, count, index, tan):
        vals = lambda i: struct.unpack_from(">h", b, t + vals_off + 2 * i)[0]
        if count == 1:
            return vals(index)
        st = 3 if tan == 0 else 4
        ks = [[vals(index + i * st + j) for j in range(st)] for i in range(count)]
        if frame <= ks[0][0]:
            return ks[0][1]
        if frame >= ks[-1][0]:
            return ks[-1][1]
        i = max(j for j in range(count) if ks[j][0] <= frame)
        a, z = ks[i], ks[i + 1]
        return herm(frame, a[0], a[1], a[2] if st == 3 else a[3], z[0], z[1], z[2])

    for kind, n, table, name_off, vo in (("reg", h(0x0C), w(0x20), w(0x30), (w(0x38), w(0x3C), w(0x40), w(0x44))),
                                          ("konst", h(0x0E), w(0x24), w(0x34), (w(0x48), w(0x4C), w(0x50), w(0x54)))):
        if not n:
            continue
        nm = names(b, t + name_off)
        for i in range(n):
            e = t + table + 28 * i
            rgba = [chan(vo[c], u16(b, e + 6 * c), u16(b, e + 6 * c + 2), u16(b, e + 6 * c + 4)) / 255.0 for c in range(4)]
            out.setdefault(nm[i], {"reg": {}, "konst": {}})[kind][b[e + 24]] = rgba
    return out


def btk(b):
    """A J3D texture SRT animation (J3D1btk1, TTK1) -> dict(frames, loop, mats: [(material name, texMtxIndex, (cx, cy),
    [scale U, scale V, rotation W, translation U, translation V])]), each track (count, tangent type, keys: floats,
    count 1 a constant else (time, value, tangent) triplets / (time, value, in, out) quads), rotations in degrees
    (s16 << rotationDecimal, 0x8000 a half turn). None if it is not one."""
    t = b.find(b"TTK1")
    if b[:8] != b"J3D1btk1" or t < 0:
        return None
    h = lambda k: u16(b, t + k)
    w = lambda k: u32(b, t + k)
    shift, n = b[t + 9], h(0x0C) // 3
    nm = names(b, t + w(0x1C))
    rk = (1 << shift) * 180.0 / 0x8000
    mats = []
    for i in range(n):
        e = t + w(0x14) + 0x36 * i
        tracks = []
        for comp, table in ((0x00, 0x28), (0x12, 0x28), (0x24 + 0x06, 0x2C), (0x0C, 0x30), (0x12 + 0x0C, 0x30)):
            count, index, tan = u16(b, e + comp), u16(b, e + comp + 2), u16(b, e + comp + 4)
            st = 1 if count == 1 else (3 if tan == 0 else 4)
            if table == 0x2C:
                vals = [struct.unpack_from(">h", b, t + w(table) + 2 * (index + j))[0] for j in range(count * st)]
                keys = [float(v) if count > 1 and j % st == 0 else v * rk for j, v in enumerate(vals)]
            else:
                keys = list(struct.unpack_from(f">{count * st}f", b, t + w(table) + 4 * index))
            tracks.append((count, tan, keys))
        cx, cy = struct.unpack_from(">2f", b, t + w(0x24) + 0x0C * i)
        mats.append((nm[i], b[t + w(0x20) + i], (cx, cy), tracks))
    return dict(frames=h(0x0A), loop=b[t + 8], mats=mats)


def tev(b, mat3, m, tex1, texs, base=None, over=None, max_stages=16, ras_k=1.0):
    """Evaluate material m's TEV stages (GX: d op (a (1 - c) + b c), bias, scale, clamp, into PREV / C0-C2) ->
    RGBA float array in 0..1: per texel of texture `base` (TEX1 index; every stage samples its own texture resized to
    it, as if all shared the UVs), or one colour (shape (4,)) when base is None (texture inputs read 1).
    MAT3 entry: tev stage count via 0x58[e+4], orders u16 @0xBC (-> {coord, map, channel}), stages u16 @0xE4 (-> 20 B:
    colour a b c d op bias scale clamp reg @1-9, alpha @10-18), konst colours u16 @0x94 (-> RGBA8 table 0x54), konst
    selectors u8 @0x9C (colour) / @0xAC (alpha), TEV colours u16 @0xDC (-> RGBA S16 table 0x50: C0, C1, C2, PREV),
    material colour u16 @8 (-> 0x20) for the rasterized colour. ponytail: no lighting (RAS = material colour), every
    stage's texture at the base texture's UVs, swap tables and indirect stages ignored."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    ns = min(b[off(0x58) + b[e + 4]], max_stages)
    shape = texs[base]["rgba"].shape[:2] if base is not None else ()

    def col(table, idx, signed):
        if idx == 0xFFFF:
            return np.zeros(4, np.float32)
        if signed:
            return np.array(struct.unpack_from(">4h", b, off(table) + 8 * idx), np.float32) / 255.0
        return np.frombuffer(b, np.uint8, 4, off(table) + 4 * idx).astype(np.float32) / 255.0

    regs = [col(0x50, u16(b, e + 0xDC + 2 * k), True) for k in range(4)]   # C0, C1, C2, PREV
    ov = over.get(material_name(b, mat3, m), {}) if over else {}  # a BRK's registers at its frame (brk)
    for k, v in ov.get("reg", {}).items():
        if k < 4:
            regs[k] = np.array(v, np.float32)
    regs = [regs[3], regs[0], regs[1], regs[2]]                              # by GX output reg: PREV, C0, C1, C2
    regs = [np.broadcast_to(r, shape + (4,)).copy() for r in regs]
    konst = [col(0x54, u16(b, e + 0x94 + 2 * k), False) for k in range(4)]
    for k, v in ov.get("konst", {}).items():
        if k < 4:
            konst[k] = np.array(v, np.float32)
    mc = u16(b, e + 8)
    ras = col(0x20, mc, False) if mc != 0xFFFF and not vertex_colour(b, mat3, m) else np.ones(4, np.float32)
    if "mat" in ov:  # MR::initDLMakerMatColor0: the actor's material colour 0
        ras = np.array(ov["mat"], np.float32)
    ras = ras * np.array([ras_k, ras_k, ras_k, 1.0], np.float32)  # a lit channel's light level (bake)
    tex_cache = {}
    order = lambda stage: u16(b, e + 0xBC + 2 * stage)
    sampled = lambda oi: u16(b, off(0x48) + 2 * u16(b, e + 0x84 + 2 * b[off(0x4C) + 4 * oi + 1]))
    base_coord = next((b[off(0x4C) + 4 * order(s)] for s in range(ns) if order(s) != 0xFFFF and
                       b[off(0x4C) + 4 * order(s) + 1] != 0xFF and sampled(order(s)) == base), None)

    def texel(stage):
        oi = order(stage)
        tmap = b[off(0x4C) + 4 * oi + 1] if oi != 0xFFFF else 0xFF
        if tmap == 0xFF:
            return np.ones(shape + (4,), np.float32)
        if ov.get("zero_coord") is not None and b[off(0x4C) + 4 * oi] == ov["zero_coord"]:
            return np.zeros(shape + (4,), np.float32)  # a layer drawn apart (env_layer): left out here
        if base is None and not ov.get("env_mean"):  # texture inputs read tex_level (default 1)
            return np.full(shape + (4,), ov.get("tex_level", 1.0), np.float32)
        t = sampled(oi)
        if base is None:  # env_mean: a texgen'd (environment) texture on an untextured group, its mean colour
            x = texs[t]["rgba"] if t < len(texs) else np.full((1, 4), 255, np.uint8)
            return x.reshape(-1, 4).mean(0).astype(np.float32) / 255.0
        # a texture on other coordinates than the base's (environment maps, scrolling sparkles, projections) cannot
        # be laid over the base's texels: its mean colour stands in
        other = t != base and b[off(0x4C) + 4 * oi] != base_coord
        if (t, other) not in tex_cache:
            x = texs[t]["rgba"] if t < len(texs) else texs[base]["rgba"]
            if other:
                x = np.broadcast_to(x.reshape(-1, 4).mean(0), shape + (4,))
            elif x.shape[:2] != shape:  # nearest resize to the base texture's size
                ys = np.arange(shape[0]) * x.shape[0] // shape[0]
                xs = np.arange(shape[1]) * x.shape[1] // shape[1]
                x = x[ys][:, xs]
            tex_cache[(t, other)] = x.astype(np.float32) / 255.0
        return tex_cache[(t, other)]

    def ksel(sel, alpha):
        if sel < 8:
            return np.full(4, KSEL_FRAC[sel], np.float32)
        if 0x0C <= sel <= 0x0F and not alpha:
            return konst[sel - 0x0C]
        if 0x10 <= sel <= 0x1F:
            c = konst[(sel - 0x10) % 4][(sel - 0x10) // 4]
            return np.full(4, c, np.float32)
        return konst[0]

    one = np.ones(shape + (4,), np.float32)
    for s in range(ns):
        st = b[off(0x5C) + 20 * u16(b, e + 0xE4 + 2 * s):][:20]
        tx = texel(s)
        kc, ka = ksel(b[e + 0x9C + s], False), ksel(b[e + 0xAC + s], True)
        prev, c0, c1, c2 = regs
        cin = [prev[..., :3], prev[..., 3:4], c0[..., :3], c0[..., 3:4], c1[..., :3], c1[..., 3:4], c2[..., :3],
               c2[..., 3:4], tx[..., :3], tx[..., 3:4], ras[:3] * one[..., :3], ras[3] * one[..., 3:4],
               one[..., :3], 0.5 * one[..., :3], kc[:3] * one[..., :3], 0 * one[..., :3]]
        ain = [prev[..., 3], c0[..., 3], c1[..., 3], c2[..., 3], tx[..., 3], ras[3] * one[..., 3], ka[3] * one[..., 3],
               0 * one[..., 3]]

        def run(a, bb, c, d, op, bias, scale, clamp):
            v = a * (1 - c) + bb * c
            v = d - v if op == 1 else d + v
            v = v + (0.5 if bias == 1 else -0.5 if bias == 2 else 0.0)
            v = v * (2.0 if scale == 1 else 4.0 if scale == 2 else 0.5 if scale == 3 else 1.0)
            return np.clip(v, 0, 1) if clamp else np.clip(v, -4, 4)

        pick = lambda lst, i: lst[i] if i < len(lst) else lst[-1]
        rgb = run(*(np.broadcast_to(pick(cin, st[k]), shape + (3,)) for k in range(1, 5)), st[5], st[6], st[7], st[8])
        al = run(*(np.broadcast_to(pick(ain, st[k]), shape) for k in range(10, 14)), st[14], st[15], st[16], st[17])
        out = regs[st[9] & 3].copy()
        out[..., :3] = rgb
        regs[st[18] & 3][..., 3] = al
        regs[st[9] & 3][..., :3] = out[..., :3]
    return np.clip(regs[0], 0, 1)


def vertex_colour(b, mat3, m):
    """True when material m's colour channel 0 is unlit and takes the vertex colour (CLR0) as its material colour
    (the converter then puts CLR0 in the vertex colours; tev reads RAS as 1). MAT3: channel indices u16 @0x0C ->
    table 0x28 of 8 B {lighting on, material colour source (1 = vertex), ...}."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    ci = u16(b, e + 0x0C)
    return ci != 0xFFFF and b[off(0x28) + 8 * ci] == 0 and b[off(0x28) + 8 * ci + 1] == 1


def lit(b, mat3, m):
    """True when material m's colour channel 0 has lighting on (MAT3 channel table, as vertex_colour)."""
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    ci = u16(b, e + 0x0C)
    return ci != 0xFFFF and b[off(0x28) + 8 * ci] != 0


def bake(b, mat3, m, tex1, texs, t, over=None, info=None):
    """Texture t with material m's TEV baked in (tev), RGBA8, keeping the texture's own alpha (cut-outs; the TEV alpha
    often reads lit channels). ponytail: a result near white where the texture is not (RAS from lit or second colour
    channels, e.g. Coin's env-map stages) falls back to the plain texture; real lighting would fix those.
    info (a dict) gets "gain": the light level the texture stands for is RAS 1 (gain 1) or RAS 0.5 (gain 2: the
    engine's lighting doubles the channel colour, as the shaders' TEX x RAS x 2 do)."""
    src = texs[t]["rgba"]
    out = (tev(b, mat3, m, tex1, texs, t, over) * 255 + 0.5).astype(np.uint8)
    if lit(b, mat3, m):
        # lit channels: the converter shades lit materials per vertex (shade), so the TEV is baked at a neutral light.
        # SMG's actor shaders double the light (TEX x RAS x 2: Mario's skin, cap, overalls), so RAS 0.5 gives back the
        # texture where RAS 1 saturates it to white; the level whose result is nearer the texture is kept (shaders
        # without the doubling stay at 1). ponytail: real lighting replaces this
        half = (tev(b, mat3, m, tex1, texs, t, over, ras_k=0.5) * 255 + 0.5).astype(np.uint8)
        d1 = abs(out[..., :3].astype(np.float32).mean() - src[..., :3].mean())
        d2 = abs(half[..., :3].astype(np.float32).mean() - src[..., :3].mean())
        if d2 < d1:
            out = half
            if info is not None:
                info["gain"] = 2
    out[..., 3] = src[..., 3]
    if out[..., :3].mean() > 240 and src[..., :3].mean() < 200:
        # saturated by stages that need real lighting (Tico's rim light): with a BRK's registers (its colour) the first
        # stage alone (TEX x RAS + C0) when that keeps a colour, else the plain texture
        one = (tev(b, mat3, m, tex1, texs, t, over, 1) * 255 + 0.5).astype(np.uint8) if over else None
        if info is not None:
            info["gain"] = 1
        if one is not None and one[..., :3].mean() <= 240:
            one[..., 3] = src[..., 3]
            return one
        return src
    return out


def samples_named(b, mat3, m, tex1, name):
    """Whether a TEV stage of material m samples the TEX1 texture called `name` (IndDummy: the screen copy J3D puts
    there at run time for indirect refraction, CrystalCage's IndTex)."""
    if tex1 is None:
        return False
    nm = names(b, tex1 + u32(b, tex1 + 0x10))
    off = lambda k: mat3 + u32(b, mat3 + k)
    e = off(0x0C) + u16(b, off(0x10) + 2 * m) * 0x14C
    for k in range(b[off(0x58) + b[e + 4]]):
        oi = u16(b, e + 0xBC + 2 * k)
        if oi == 0xFFFF or b[off(0x4C) + 4 * oi + 1] == 0xFF:
            continue
        t = u16(b, off(0x48) + 2 * u16(b, e + 0x84 + 2 * b[off(0x4C) + 4 * oi + 1]))
        if t < len(nm) and nm[t] == name:
            return True
    return False


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
    # 1-texel red / black stripes: 3 mips, level 1 one in-between red from the shared palette, the base keeps both
    st = np.zeros((64, 64, 4), np.uint8)
    st[..., 3], st[::2, :, 0] = 255, 255
    t = to_ps2(st, 1, 1)
    red = lambda pix: {int(np.frombuffer(t["clut"], "<u4")[i]) & 255 for i in set(np.frombuffer(pix, np.uint8) & 15)}
    assert len(t["mips"]) == 3 and red(t["pix"]) == {0, 255} and red(t["mips"][0]) == {128}, (red(t["pix"]), red(t["mips"][0]))
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
    # PSMT8: 256 x 1 grey ramp keeps 256 levels; CLUT entry 8 holds the colour of index 16 (CSM1)
    t = np.zeros((1, 256, 4), np.uint8)
    t[..., 0] = t[..., 1] = t[..., 2] = np.arange(256)
    t[..., 3] = 255
    p = to_ps2(t, 0, 0, hi=True)
    clut = np.frombuffer(p["clut"], "<u4")
    assert p["psm"] == PSMT8 and len(p["pix"]) == 256 and len(set(p["pix"])) > 200
    k = p["pix"][100]
    csm1 = k ^ ((k >> 3 ^ k >> 4) & 1) * 0x18
    assert abs((clut[csm1] & 0xFF) - 100) <= 2, (clut[csm1] & 0xFF)
    # Maya SRT with scale 2, no rotation/translation: s' = 2 s, t' = 2 t - 1 (J3DGetTextureMtxMaya)
    e = bytearray(0x64)
    e[1] = 0x80
    struct.pack_into(">2f", e, 0x10, 2.0, 2.0)
    m = tex_mtx(bytes(e), 0)
    assert abs(m[0][0] - 2) < 1e-6 and abs(m[0][2]) < 1e-6 and abs(m[1][1] - 2) < 1e-6 and abs(m[1][2] + 1) < 1e-6, m
    # TEV: one stage, colour = KONST (selector K0) * TEXC; texture [black, white] -> [0, konst]; no texture -> konst
    mb = bytearray(0x400)
    for k, o in ((0x0C, 0x100), (0x10, 0x300), (0x58, 0x310), (0x4C, 0x320), (0x48, 0x330), (0x54, 0x340),
                 (0x50, 0x350), (0x20, 0x360), (0x5C, 0x370)):
        struct.pack_into(">I", mb, k, o)
    mb[0x100:0x24C] = bytes([255]) * 0x14C
    mb[0x104], mb[0x310], mb[0x19C] = 0, 1, 0x0C
    for f in (0x08, 0x84, 0x94, 0xBC, 0xE4):
        struct.pack_into(">H", mb, 0x100 + f, 0)
    struct.pack_into(">H", mb, 0x300, 0)
    mb[0x320:0x324] = bytes((0, 0, 4, 255))
    mb[0x340:0x344] = bytes((0, 35, 157, 255))
    mb[0x360:0x364] = bytes([255]) * 4
    mb[0x370:0x384] = bytes.fromhex("ff0f0e080f0000000100070707070000000100ff")
    tx = np.zeros((1, 2, 4), np.uint8)
    tx[0, 1] = 255
    r = tev(bytes(mb), 0, 0, None, [{"rgba": tx}], 0)
    assert np.allclose(r[0, 0, :3], 0) and np.allclose(r[0, 1, :3] * 255, (0, 35, 157)), r
    assert np.allclose(tev(bytes(mb), 0, 0, None, [])[:3] * 255, (0, 35, 157))
    print("selftest ok")


if __name__ == "__main__":
    selftest()
