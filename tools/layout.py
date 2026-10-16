"""2D layouts (nw4r lyt BRLYT + TPL, Petari's LayoutActor screens) as flat textured quads for the engine.

LAYO section (in resident's FONT chunk, msg.font_chunk): u16 nlayouts, u16 pad; then per layout: char name[24],
u16 nquads, u16 pad, nquads x Quad {s16 x, y (top left, layout units: the screen centre the origin, y up), s16 w, h,
u16 tex (index among the FONT chunk's textures, 0xFFFF none), u8 rgba[4] (0x80 = 1.0 for colour and alpha), s16 u, v,
uw, vh (texels of the PS2 texture x 16; negative widths flip), u8 group (which child of the root pane it is under:
lay_draw draws one group or all), u8 fill (0, or 1 + k: the layout's k-th masked pane, turned by the engine), s16 cu,
cv, tu, tv (a masked pane's texture SRT centre and translation, texels x 16)} (32 B), in drawing order. A masked pane
goes out as the part of it its mask shows, with u / v / uw / vh its first texture's coordinates before the SRT's
rotation (scaled about the centre); the engine turns them by the pane's animated angle: s = cu + tu + cos (u - cu) -
sin (v - cv), t = cv + tv + sin (u - cu) + cos (v - cv).
Panes: pic1 a quad of its first material texture; wnd1 nine (lyt::Window with one frame texture: corners at the
texture's size, flipped round, edges stretching its inner column / row, the content in between); pan1 / txt1 / bnd1
nothing (their children are walked). A pane's translation is in its parent's space (scaled by the parents' scales),
its origin (0..8: x left / centre / right, y top / centre / bottom) places its size; alpha multiplies down the tree
where a pane influences its children's alpha (flags bit 1). Materials: the texture through lerp(black colour C0,
white colour C1, texel) (lyt's default TEV for one texture; I formats are their intensity in every channel); without
a texture the colour is the pane's vertex colours. A layout with an appear.brlan is taken at its last frame (RLPA
translation / scale / size, RLVC alpha: StarPieceTargetCounter slides in from x 500 to 276); a group pane (pan1, a
child of the root pane) at alpha 0 otherwise is taken as shown (its appear animation brings it in:
InformationWindow). A pane rotated 180 about y is mirrored. Pictures whose material masks its first texture with a
second (StarPieceTargetMeter's fill halves, TalkBalloonEvent's beak and its shadow) are masked panes: their texture
baked white (the material's colour in the quad's), their rectangle where the mask (through its texture SRT 1) shows.
A material's texture SRT is baked into its image (StarPieceTargetMeter's frame: scale 2 mirrored, a whole ring).
ponytail: no other rotation, no other animated texture SRT or projections, vertex colours averaged, the brlan
animations not played (but appear's end and the masked panes' angles).
"""
import math
import struct
from pathlib import Path

import numpy as np

import budget as B
import tex

u16 = lambda b, o: struct.unpack_from(">H", b, o)[0]
u32 = lambda b, o: struct.unpack_from(">I", b, o)[0]


def tpl(b):
    """A TPL's first image -> RGBA (I4 / I8 their intensity as colour and alpha, as layouts use them)."""
    ih = u32(b, u32(b, 8))
    h, w, fmt, off = struct.unpack_from(">HHII", b, ih)
    rgba = tex.decode(b[off:], fmt, w, h).copy()
    if fmt in (0, 1):
        rgba[..., 3] = rgba[..., 0]
    return rgba


def parse(b):
    """BRLYT -> (textures, materials, panes): materials {name, c0, c1 (RGBA 0..1), tex (txl1 indices)}, panes in
    file order {kind, name, flags, origin, alpha, t (x, y), s (x, y), size (w, h), parent, mat, colours, wnd}."""
    texs, mats, panes, stack, last = [], [], [], [], None
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind == b"txl1":
            texs = [b[o + 12 + u32(b, o + 12 + 8 * i):].split(b"\0")[0].decode() for i in range(u16(b, o + 8))]
        elif kind == b"mat1":
            for i in range(u16(b, o + 8)):
                mo = o + u32(b, o + 12 + 4 * i)
                c0 = np.clip(np.array(struct.unpack_from(">4h", b, mo + 0x14), np.float32) / 255, 0, 1)
                c1 = np.clip(np.array(struct.unpack_from(">4h", b, mo + 0x1C), np.float32) / 255, 0, 1)
                nt, ns = u32(b, mo + 0x3C) & 0xF, u32(b, mo + 0x3C) >> 4 & 0xF
                so = mo + 0x40 + 4 * nt
                mats.append(dict(name=b[mo:mo + 20].split(b"\0")[0].decode().strip(), c0=c0, c1=c1,
                                 tex=[u16(b, mo + 0x40 + 4 * k) for k in range(nt)],
                                 wrap=b[mo + 0x42:mo + 0x44] if nt else b"\0\0",
                                 srt=struct.unpack_from(">5f", b, so) if ns else (0, 0, 0, 1, 1),
                                 srt1=struct.unpack_from(">5f", b, so + 20) if ns > 1 else (0, 0, 0, 1, 1)))
        elif kind in (b"pan1", b"pic1", b"wnd1", b"txt1", b"bnd1"):
            p = dict(kind=kind.decode(), name=b[o + 12:o + 28].split(b"\0")[0].decode(), flags=b[o + 8], origin=b[o + 9],
                     alpha=b[o + 10] / 255, t=struct.unpack_from(">2f", b, o + 0x24), s=struct.unpack_from(">2f", b, o + 0x3C),
                     size=struct.unpack_from(">2f", b, o + 0x44), ry=struct.unpack_from(">f", b, o + 0x34)[0],
                     parent=stack[-1] if stack else None, mat=None,
                     colours=None, wnd=None)
            if kind == b"pic1":
                p["colours"] = [struct.unpack_from(">4B", b, o + 0x4C + 4 * k) for k in range(4)]
                p["mat"] = u16(b, o + 0x5C)
                p["uv"] = struct.unpack_from(">8f", b, o + 0x60) if b[o + 0x5E] else (0, 0, 1, 0, 0, 1, 1, 1)
            elif kind == b"wnd1":
                co = o + u32(b, o + 0x60)
                fo = o + u32(b, o + 0x64)
                frames = [(u16(b, o + u32(b, fo + 4 * k)), b[o + u32(b, fo + 4 * k) + 2]) for k in range(b[o + 0x5C])]
                p["colours"] = [struct.unpack_from(">4B", b, co + 4 * k) for k in range(4)]
                p["mat"] = u16(b, co + 16)
                p["wnd"] = dict(inflate=struct.unpack_from(">4f", b, o + 0x4C), frames=frames)
            elif kind == b"txt1":  # lyt::TextBox: its material, text position (0..8), font size, character space
                p["mat"] = u16(b, o + 0x50)
                p["txt"] = dict(pos=b[o + 0x54], size=struct.unpack_from(">2f", b, o + 0x64),
                                space=struct.unpack_from(">f", b, o + 0x6C)[0])
            panes.append(p)
            last = len(panes) - 1
        elif kind == b"pas1":
            stack.append(last)
        elif kind == b"pae1":
            stack.pop()
        o += size
    return texs, mats, panes


def brlan_end(b):
    """A BRLAN's panes at its last frame -> {pane name: {target: value}}: RLPA (0..9: translation x y z, rotation x y
    z, scale x y, size w h) and RLVC 16 (alpha 0..255); Hermite keys {frame, value, slope}, the value held past the
    last key (nw4r lyt AnimTransformBasic)."""
    out = {}
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind == b"pai1":
            frames, nent, eoff = u16(b, o + 8), u16(b, o + 14), u32(b, o + 16)
            for e in range(nent):
                eo = o + u32(b, o + eoff + 4 * e)
                name = b[eo:eo + 20].split(b"\0")[0].decode()
                if b[eo + 21]:  # a material's
                    continue
                for t in range(b[eo + 20]):
                    to = eo + u32(b, eo + 24 + 4 * t)
                    tag = b[to:to + 4]
                    for i in range(b[to + 4]):
                        io = to + u32(b, to + 8 + 4 * i)
                        target, ctype, nk = b[io + 1], b[io + 2], u16(b, io + 4)
                        ko = io + u32(b, io + 8)
                        if ctype != 2 or not nk or (tag == b"RLVC" and target != 16) or tag not in (b"RLPA", b"RLVC"):
                            continue
                        keys = [struct.unpack_from(">3f", b, ko + 12 * k) for k in range(nk)]
                        val = keys[-1][1]
                        for (f0, v0, s0), (f1, v1, s1) in zip(keys, keys[1:]):
                            if f0 <= frames <= f1 and f1 > f0:  # Hermite between the two keys at the end frame
                                t_ = (frames - f0) / (f1 - f0)
                                h1, h2 = 2 * t_ ** 3 - 3 * t_ ** 2 + 1, -2 * t_ ** 3 + 3 * t_ ** 2
                                h3, h4 = t_ ** 3 - 2 * t_ ** 2 + t_, t_ ** 3 - t_ ** 2
                                val = h1 * v0 + h2 * v1 + (h3 * s0 + h4 * s1) * (f1 - f0)
                        out.setdefault(name, {})[(tag, target)] = val
        o += size
    return out


def srt_bake(t, srt, wrap):
    """a texture (h, w, 4) seen through its texture SRT (tx, ty, rotation degrees, sx, sy: nw4r lyt CalcTextureMtx
    about the centre) over the pane's uv 0..1 -> an image of (h, w) x the scale, nearest texels; wrap per axis 0 clamp,
    1 repeat, 2 mirror"""
    tx, ty, r, sx, sy = srt
    h, w = t.shape[:2]
    oh, ow = max(1, round(h * abs(sy))), max(1, round(w * abs(sx)))
    v, u = np.meshgrid((np.arange(oh) + 0.5) / oh - 0.5, (np.arange(ow) + 0.5) / ow - 0.5, indexing="ij")
    c, sn = np.cos(np.radians(r)), np.sin(np.radians(r))
    s_, t_ = 0.5 + tx + c * sx * u - sn * sy * v, 0.5 + ty + sn * sx * u + c * sy * v

    def wr(x, mode):
        return x % 1 if mode == 1 else 1 - np.abs(x % 2 - 1) if mode == 2 else np.clip(x, 0, 1)
    xi = np.clip((wr(s_, wrap[0]) * w).astype(int), 0, w - 1)
    yi = np.clip((wr(t_, wrap[1]) * h).astype(int), 0, h - 1)
    return t[yi, xi]


def mask_box(mask, srt, wrap, u0, v0, u1, v1, n=64):
    """the pane fractions (x0, y0, x1, y1; y down) where a mask texture, through its texture SRT over the pane's uv
    (u0, v0) top left .. (u1, v1) bottom right, has alpha over half"""
    tx, ty, r, sx, sy = srt
    f = (np.arange(n) + 0.5) / n
    yf, xf = np.meshgrid(f, f, indexing="ij")
    u, v = u0 + xf * (u1 - u0) - 0.5, v0 + yf * (v1 - v0) - 0.5
    c, sn = np.cos(np.radians(r)), np.sin(np.radians(r))
    s_, t_ = 0.5 + tx + c * sx * u - sn * sy * v, 0.5 + ty + sn * sx * u + c * sy * v

    def wr(x, mode):
        return x % 1 if mode == 1 else 1 - np.abs(x % 2 - 1) if mode == 2 else np.clip(x, 0, 1)
    h, w = mask.shape[:2]
    on = mask[np.clip((wr(t_, wrap[1]) * h).astype(int), 0, h - 1), np.clip((wr(s_, wrap[0]) * w).astype(int), 0, w - 1), 3] > 127
    ys, xs = np.nonzero(on)
    return round(xs.min() / n, 3), round(ys.min() / n, 3), round((xs.max() + 1) / n, 3), round((ys.max() + 1) / n, 3)


def quads(files, name):
    """Layout `name` of an archive's files ({lower name: bytes}) -> ([quad tuples], [RGBA textures]): quad (x, y, w,
    h, tex, rgba, u0, v0, u1, v1, group, fill, cu, cv, tu, tv) in layout units, tex an index into the textures list."""
    texs, mats, panes = parse(files[name.lower() + ".brlyt"])
    out, images, baked = [], [], {}
    end = brlan_end(files["appear.brlan"]) if "appear.brlan" in files else {}
    for p in panes:
        for (tag, target), v in end.get(p["name"], {}).items():
            if tag == b"RLVC":
                p["alpha"] = max(0.0, min(1.0, v / 255))
            elif target in (0, 1):
                p["t"] = tuple(v if k == target else p["t"][k] for k in range(2))
            elif target in (6, 7):
                p["s"] = tuple(v if k == target - 6 else p["s"][k] for k in range(2))
            elif target in (8, 9):
                p["size"] = tuple(v if k == target - 8 else p["size"][k] for k in range(2))

    def image(mi, white=False):
        """material mi's first texture through lerp(C0, C1, texel) (white: lerp(white at alpha 0, white)) -> index in
        images (None: no texture)"""
        m = mats[mi]
        if not m["tex"] or m["tex"][0] >= len(texs):
            return None
        if mi not in baked:
            t = tpl(files[texs[m["tex"][0]].lower()]).astype(np.float32) / 255
            c0, c1 = (np.array([1, 1, 1, 0], np.float32), np.ones(4, np.float32)) if white else (m["c0"], m["c1"])
            if not white and tuple(m["srt"]) != (0, 0, 0, 1, 1):
                t = srt_bake(t, m["srt"], m["wrap"])
            img = c0 + (c1 - c0) * t
            baked[mi] = len(images)
            images.append((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))
        return baked[mi]

    groups = [i for i, p in enumerate(panes) if p["parent"] == 0]  # the root pane's children
    for i in groups:
        if panes[i]["alpha"] == 0 and panes[i]["kind"] == "pan1" and panes[i]["name"] not in end:
            panes[i]["alpha"] = 1.0
    nfill = 0
    absolute = []  # per pane: its origin's position, its scale, its alpha, shown, group
    for p in panes:
        if p["parent"] is None:
            pos, sc, al, shown, grp = (0.0, 0.0), (1.0, 1.0), 1.0, True, 0
        else:
            ppos, psc, pal, pshown, pgrp = absolute[p["parent"]]
            par = panes[p["parent"]]
            pos = (ppos[0] + p["t"][0] * psc[0], ppos[1] + p["t"][1] * psc[1])
            sc = (psc[0] * p["s"][0], psc[1] * p["s"][1])
            al = pal * (par["alpha"] if par["flags"] & 2 else 1.0)
            shown = pshown and bool(par["flags"] & 1)
            grp = groups.index(len(absolute)) if p["parent"] == 0 else pgrp
        absolute.append((pos, sc, al, shown, grp))
        if p["kind"] == "txt1" and shown and p["flags"] & 1 and p["mat"] < len(mats):
            # a text box (the engine writes its number): fill 255, its box, the material's white colour (lerp(C0,
            # C1, glyph) with C0 clear: the glyph times C1), font size, character space and text position
            w, h = p["size"][0] * sc[0], p["size"][1] * sc[1]
            ox, oy = p["origin"] % 3, p["origin"] // 3
            c1 = mats[p["mat"]]["c1"]
            rgba = tuple(int(min(255, c * 128 + 0.5)) for c in (c1[0], c1[1], c1[2], c1[3] * al * p["alpha"]))
            t = p["txt"]
            out.append((pos[0] - w * ox / 2, pos[1] + h * oy / 2, w, h, None, rgba, t["size"][0] * sc[0],
                        t["size"][1] * sc[1], t["space"], t["pos"], grp, 255, 0, 0, 0, 0))
            continue
        if not (shown and p["flags"] & 1) or p["kind"] not in ("pic1", "wnd1") or p["mat"] is None or p["mat"] >= len(mats):
            continue
        m = mats[p["mat"]]
        fill = len(m["tex"]) > 1 and p["kind"] == "pic1" and m["tex"][1] < len(texs)
        nfill += fill
        w, h = p["size"][0] * sc[0], p["size"][1] * sc[1]
        ox, oy = p["origin"] % 3, p["origin"] // 3
        mirror = abs(abs(p["ry"]) - 180) < 1e-3
        if mirror:
            ox = 2 - ox
        x0 = pos[0] - w * ox / 2
        y0 = pos[1] + h * oy / 2  # top (y up)
        a = al * p["alpha"]
        col = np.mean(np.array(p["colours"], np.float32), 0) / 255
        ti = image(p["mat"], fill)
        if ti is None or fill:  # no texture (or baked white): the white colour times the vertex colours
            col = col * m["c1"]
        rgba = tuple(int(min(255, c * 128 + 0.5)) for c in (col[0], col[1], col[2], col[3] * a))
        col0 = np.mean(np.array(p["colours"], np.float32), 0) / 255  # the frames' (textured) vertex colour
        if p["kind"] == "pic1":
            uv = p["uv"]
            u0, u1 = (uv[2], uv[0]) if mirror else (uv[0], uv[2])
            if not fill:
                out.append((x0, y0, w, h, ti, rgba, u0, uv[1], u1, uv[7], grp, 0, 0, 0, 0, 0))
                continue
            mk = tpl(files[texs[m["tex"][1]].lower()])
            bx0, by0, bx1, by1 = mask_box(mk, m["srt1"], m["wrap"], u0, uv[1], u1, uv[7])
            tx, ty, _, sx, sy = m["srt"]

            def pre(xf, yf):  # the first texture's coordinates before the rotation at a pane fraction
                return (0.5 + sx * (u0 + xf * (u1 - u0) - 0.5), 0.5 + sy * (uv[1] + yf * (uv[7] - uv[1]) - 0.5))
            (a0, b0), (a1, b1) = pre(bx0, by0), pre(bx1, by1)
            out.append((x0 + bx0 * w, y0 - by0 * h, (bx1 - bx0) * w, (by1 - by0) * h, ti, rgba, a0, b0, a1, b1, grp,
                        nfill, 0.5, 0.5, tx, ty))
            continue
        fr = p["wnd"]["frames"]
        fi = image(fr[0][0]) if fr and fr[0][0] < len(mats) else None
        if fi is None:
            out.append((x0, y0, w, h, ti, rgba, 0, 0, 1, 1, grp, 0, 0, 0, 0, 0))
            continue
        fh_, fw_ = images[fi].shape[:2]
        fw, fh = min(fw_, w / 2), min(fh_, h / 2)
        iu, iv = 1 - 0.5 / fw_, 1 - 0.5 / fh_  # the corner texture's inner column / row
        if rgba[3]:
            out.append((x0 + fw, y0 - fh, w - 2 * fw, h - 2 * fh, ti, rgba, 0, 0, 1, 1, grp, 0, 0, 0, 0, 0))  # content
        rgba = tuple(int(min(255, c * 128 + 0.5)) for c in (col0[0], col0[1], col0[2], col0[3] * a))
        out += [(x0, y0, fw, fh, fi, rgba, 0, 0, 1, 1, grp, 0, 0, 0, 0, 0),                                  # corners
                (x0 + w - fw, y0, fw, fh, fi, rgba, 1, 0, 0, 1, grp, 0, 0, 0, 0, 0),
                (x0, y0 - h + fh, fw, fh, fi, rgba, 0, 1, 1, 0, grp, 0, 0, 0, 0, 0),
                (x0 + w - fw, y0 - h + fh, fw, fh, fi, rgba, 1, 1, 0, 0, grp, 0, 0, 0, 0, 0),
                (x0 + fw, y0, w - 2 * fw, fh, fi, rgba, iu, 0, iu, 1, grp, 0, 0, 0, 0, 0),                   # top, bottom edges
                (x0 + fw, y0 - h + fh, w - 2 * fw, fh, fi, rgba, iu, 1, iu, 0, grp, 0, 0, 0, 0, 0),
                (x0, y0 - fh, fw, h - 2 * fh, fi, rgba, 0, iv, 1, iv, grp, 0, 0, 0, 0, 0),                   # left, right edges
                (x0 + w - fw, y0 - fh, fw, h - 2 * fh, fi, rgba, 1, iv, 0, iv, grp, 0, 0, 0, 0, 0)]
    return out, images


def anim_at(b, frame):
    """A BRLAN's values at a frame -> {(pane name, is material, tag, target): value}: the last key at or before the
    frame (step keys: their later twin), Hermite between two distinct key frames."""
    out = {}
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind == b"pai1":
            nent, eoff = u16(b, o + 14), u32(b, o + 16)
            for e in range(nent):
                eo = o + u32(b, o + eoff + 4 * e)
                name = b[eo:eo + 20].split(b"\0")[0].decode()
                for t in range(b[eo + 20]):
                    to = eo + u32(b, eo + 24 + 4 * t)
                    tag = b[to:to + 4]
                    for i in range(b[to + 4]):
                        io = to + u32(b, to + 8 + 4 * i)
                        target, ctype, nk = b[io + 1], b[io + 2], u16(b, io + 4)
                        ko = io + u32(b, io + 8)
                        if ctype == 1:  # step keys (RLVI)
                            keys = [struct.unpack_from(">fH", b, ko + 8 * k) for k in range(nk)]
                            val = keys[0][1]
                            for f, v in keys:
                                if f <= frame:
                                    val = v
                        else:
                            keys = [struct.unpack_from(">3f", b, ko + 12 * k) for k in range(nk)]
                            val = keys[0][1]
                            for (f0, v0, s0), (f1, v1, s1) in zip(keys, keys[1:]):
                                if f0 <= frame < f1:
                                    t_ = (frame - f0) / (f1 - f0)
                                    h1, h2 = 2 * t_ ** 3 - 3 * t_ ** 2 + 1, -2 * t_ ** 3 + 3 * t_ ** 2
                                    val = h1 * v0 + h2 * v1 + ((t_ ** 3 - 2 * t_ ** 2 + t_) * s0 + (t_ ** 3 - t_ ** 2) * s1) * (f1 - f0)
                            if frame >= keys[-1][0]:
                                val = keys[-1][1]
                        out[(name, bool(b[eo + 21]), tag, target)] = val
        o += size
    return out


def bake_meter(fsp):
    """HitPointMeter (MeterLayout) for life 0..3 as images: its HitPoint pane (shadow, base, the three pieces shown
    and coloured by count.brlan at the life, the highlight) and the LIFE title (MenuFont64), drawn in software with
    the panes' rotations -> ([RGBA images], (left, top) of the images in layout units, the number's text boxes as
    quads.py text records). ponytail: the 4..6 (LifeUp) W meter, the Danger / Damage animations"""
    import msg
    files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
             B.rarc_files((Path(fsp) / "LayoutData" / "HitPointMeter.arc").read_bytes())}
    b = files["hitpointmeter.brlyt"]
    texs, mats, panes = parse(b)
    rz, o = [], u16(b, 0xC)  # each pane's z rotation (file order, as parse)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind in (b"pan1", b"pic1", b"wnd1", b"txt1", b"bnd1"):
            rz.append(struct.unpack_from(">f", b, o + 0x38)[0])
        o += size
    W, H, X0, Y0 = 128, 128, 223 - 64, 139 + 70  # around HitPoint (223, 139), the title above
    font = msg.rfnt(msg._arc(fsp, "LayoutData/Font.arc")["menufont64.brfnt"])
    imgs = []
    for life in range(4):
        an = anim_at(files["count.brlan"], life)
        canvas = np.zeros((H, W, 4), np.float32)
        mtx = []  # per pane: 2x3 affine (layout units), visible, alpha
        for i, p in enumerate(panes):
            vis = an.get((p["name"], False, b"RLVI", 0), p["flags"] & 1)
            a = math.radians(rz[i])
            m = np.array([[math.cos(a) * p["s"][0], -math.sin(a) * p["s"][1], p["t"][0]],
                          [math.sin(a) * p["s"][0], math.cos(a) * p["s"][1], p["t"][1]]])
            if p["parent"] is not None:
                pm, pvis = mtx[p["parent"]]
                m = pm @ np.vstack([m, [0, 0, 1]])
                vis = vis and pvis
            mtx.append((m, vis))
            under = i
            while under is not None and panes[under]["name"] not in ("HitPoint", "Font"):
                under = panes[under]["parent"]
            if not vis or under is None or p["kind"] not in ("pic1", "txt1") or p["name"].endswith("W"):
                continue
            mt = mats[p["mat"]]
            c0, c1 = mt["c0"].copy(), mt["c1"].copy()
            for k in range(4):
                c1[k] = an.get((p["name"], True, b"RLMC", 8 + k), c1[k] * 255) / 255
            if p["kind"] == "pic1":
                t = tpl(files[texs[mt["tex"][0]].lower()]).astype(np.float32) / 255
                if tuple(mt["srt"]) != (0, 0, 0, 1, 1):  # the pieces: s mirrored round its middle (wedge)
                    t = srt_bake(t, mt["srt"], mt["wrap"])
                img = c0 + (c1 - c0) * t
                ox, oy = p["origin"] % 3, p["origin"] // 3
                w, h = p["size"]
                _blit(canvas, img, m, -w * ox / 2, h * oy / 2, w, h, X0, Y0)
            elif p["name"] == "TxtTitle":  # "LIFE", centred, 64 px glyphs, -3 spacing; colours top white, bottom 120
                gl = [(font["cmap"][ord(ch)], font["widths"].get(font["cmap"][ord(ch)], font["default"])) for ch in "LIFE"]
                tw = sum(cw for _, (_, _, cw) in gl) - 3 * (len(gl) - 1)
                x = -tw / 2
                for gi, (left, gw, cw) in gl:
                    g = msg.glyph_image(font, gi)[:, :max(gw, 1)].astype(np.float32) / 255
                    ramp = np.linspace(1.0, 120 / 255, g.shape[0])[:, None, None]
                    col = (c0 + (c1 - c0) * g) * np.concatenate([np.repeat(ramp, 3, 2), np.ones_like(ramp)], 2)
                    _blit(canvas, col, m, x + left, 32, g.shape[1], g.shape[0], X0, Y0)
                    x += cw - 3
        imgs.append((np.clip(canvas, 0, 1) * 255 + 0.5).astype(np.uint8))
    return imgs, (X0, Y0)


def _blit(canvas, img, m, x, y, w, h, X0, Y0):
    """Composite img (RGBA 0..1) as the pane-local rectangle (x, y) top left, w x h (y up) under affine m onto the
    canvas (top left at layout (X0, Y0)), bilinear, over."""
    H, W = canvas.shape[:2]
    inv = np.linalg.inv(np.vstack([m, [0, 0, 1]]))
    jj, ii = np.mgrid[0:H, 0:W]
    X, Y = X0 + ii + 0.5, Y0 - jj - 0.5
    lx = inv[0, 0] * X + inv[0, 1] * Y + inv[0, 2]
    ly = inv[1, 0] * X + inv[1, 1] * Y + inv[1, 2]
    u = (lx - x) / w * img.shape[1] - 0.5
    v = (y - ly) / h * img.shape[0] - 0.5
    ins = (u > -1) & (v > -1) & (u < img.shape[1]) & (v < img.shape[0])
    u0, v0 = np.floor(u).astype(int), np.floor(v).astype(int)
    fu, fv = (u - u0)[..., None], (v - v0)[..., None]
    def at(a, b_):
        return img[np.clip(b_, 0, img.shape[0] - 1), np.clip(a, 0, img.shape[1] - 1)] * \
            ((a >= 0) & (a < img.shape[1]) & (b_ >= 0) & (b_ < img.shape[0]))[..., None]
    s = (at(u0, v0) * (1 - fu) * (1 - fv) + at(u0 + 1, v0) * fu * (1 - fv) + at(u0, v0 + 1) * (1 - fu) * fv +
         at(u0 + 1, v0 + 1) * fu * fv) * ins[..., None]
    a = s[..., 3:4]
    canvas[..., :3] = s[..., :3] * a + canvas[..., :3] * (1 - a)
    canvas[..., 3:4] = a + canvas[..., 3:4] * (1 - a)


# drawn by game.c lay_draw: the talk balloons (talk_draw), InformationWindow (group 0 InfoWindowUp: TicoFat's Info),
# TicoFat's FullnessMeter (StarPieceTargetMeter group 0 at the pointer, StarPieceTargetCounter), the HUD counters
LAYOUTS = ("TalkBalloonEvent", "SignBoardBalloon", "InformationWindow", "StarPieceTargetMeter", "StarPieceTargetCounter",
           "StarCounter", "CoinCounter", "StarPieceCounter", "PlayerLeft", "HitPointMeter")  # the HUD: game.c hud_draw


def section(fsp, first_tex):
    """LAYO section for LAYOUTS -> (bytes, [PS2 textures (tex.to_ps2, clamped)], [RGBA images]); quads' textures
    numbered from first_tex."""
    head, body, ps2, images = struct.pack("<HH", len(LAYOUTS), 0), b"", [], []
    for name in LAYOUTS:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(fsp) / "LayoutData" / f"{name}.arc").read_bytes())}
        qs, imgs = quads(files, name)
        if name == "HitPointMeter":  # baked per life (group = life), its number's text boxes (not LIFE's) group 15
            meter, (x0, y0) = bake_meter(fsp)
            qs = [(x0, y0, im.shape[1], im.shape[0], k, (128, 128, 128, 128), 0, 0, 1, 1, k, 0, 0, 0, 0, 0)
                  for k, im in enumerate(meter)] +                  [q[:5] + (q[5][:3] + (128 if q[5][0] else 50,),) + q[6:10] + (15,) + q[11:]  # HPMeter's alpha 0:
                  for q in qs if q[11] == 255 and q[7] > 40]  # ShowHide drives the alpha; the number (32 x 1.4), not LIFE
            imgs = meter
        base = len(ps2)
        ps2 += [tex.to_ps2(im, 0, 0, hi=True) for im in imgs]
        images += imgs
        body += struct.pack("<24sHH", name.encode(), len(qs), 0)
        for x, y, w, h, ti, rgba, u0, v0, u1, v1, grp, fill, cu, cv, tu, tv in qs:
            if fill == 255:  # a text box: u v the font size x 16, uw the character space x 16, vh the text position
                body += struct.pack("<4hH4B4h2B4h", round(x), round(y), round(w), round(h), 0xFFFF, *rgba,
                                    round(16 * u0), round(16 * v0), round(16 * u1), v1, grp, fill, 0, 0, 0, 0)
                continue
            t = ps2[base + ti] if ti is not None else None
            sw, sh = (t["w"] * t["u_scale"], t["h"] * t["v_scale"]) if t else (0, 0)
            body += struct.pack("<4hH4B4h2B4h", round(x), round(y), round(w), round(h),
                                0xFFFF if ti is None else first_tex + base + ti, *rgba,
                                round(16 * u0 * sw), round(16 * v0 * sh), round(16 * (u1 - u0) * sw), round(16 * (v1 - v0) * sh),
                                grp, fill, round(16 * cu * sw), round(16 * cv * sh), round(16 * tu * sw), round(16 * tv * sh))
    return head + body, ps2, images


if __name__ == "__main__":  # python tools/layout.py data/fs/files: each layout's quads, its textures as PNGs in out/
    import sys
    from PIL import Image
    data, _, imgs = section(sys.argv[1], 0)
    for i, im in enumerate(imgs):
        Image.fromarray(im, "RGBA").save(f"out/layout_tex{i}.png")
    for name in LAYOUTS:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(sys.argv[1]) / "LayoutData" / f"{name}.arc").read_bytes())}
        for q in quads(files, name)[0]:
            print(name, [round(v, 2) if isinstance(v, float) else v for v in q])
    print(len(data), "bytes LAYO,", len(imgs), "textures")
