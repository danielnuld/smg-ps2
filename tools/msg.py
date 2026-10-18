"""Talk messages and their font (Petari MessageHolder.cpp, TalkNodeCtrl.cpp, nw4r ResFont).

FONT chunk (resident.bin, after the effects' textures in its TEXT):
  u16 ntext, u16 nicon, u16 text_tex, u16 icon_tex (TEXT indices in that blob), s8 line_feed, u8 ascent, u8 cell_h,
  u8 icon_cell_h, u32 LAYO offset (tools/layout.py, the talk balloons); then ntext + nicon x Glyph {u16 code, u16 u, u16 v, u8 w, u8 h, s8 left, u8 glyph_w,
  s8 char_w, u8 pad} (sorted by code within each set; icons by their tag id, picture font code - '0').
  The text atlas is PSMT8 256 x 256, its CLUT white with the I4 intensity as alpha (tint by vertex colour); the icons' a hi
  to_ps2 texture.
TALK chunk (scene.bin): u16 nctl, u16 nnode, u16 nmsg, u16 pad, u32 text bytes, 4 pad;
  nctl x Ctl {char label[32], u16 row (OBJS index, 0xFFFF a Common_ label), u16 node (0xFFFF none: msg direct),
              u16 msg, u16 pad}
  nnode x Node {u8 type (1 message, 2 branch, 3 event), u8 group, u16 index (message: compact id; branch: its
               condition; event: FLW1 index), u16 next (message: node; branch: its argument), u16 t0, u16 t1 (branch:
               yes / no nodes; event: its branch target), u16 pad}   0xFFFF none
  nmsg x Msg {u32 text offset (u16 units), u16 camera set, u8 sound, u8 camera type, u8 talk type, u8 balloon,
              s8 _A, s8 _B}
  text: u16 LE units, 0 ends a message; a tag is 0x1A, group, type, n bytes, then n bytes (padded to even) as they
  were in the BMG.
"""
import struct
from pathlib import Path

import numpy as np

import budget as B
import layout

LANG = "UsSpanish"  # MessageData language (UsEnglish / UsFrench / UsSpanish)
# IconAButton openWithTalk / openWithRead, the player name tag (ReplaceTagProcessor: System_PlayerName0<form>)
SYSTEM = ("Layout_SystemTalk", "Layout_SystemRead", "System_PlayerName000")
# createTalkCtrlDirect; TicoFat's InformationPlanet (appearInformation: getGameMessageDirect)
# createTalkCtrlDirect bound to the placed actor (Butler::initTalkCtrlArray's cMessageId, initForAstroDome's demo talks)
DIRECT = {"Butler": ("AstroDome_Butler001", "AstroDome_Butler002", "AstroDome_Butler003", "AstroDome_Butler006",
                     "AstroDome_Butler007", "AstroGalaxy_Butler005", "AstroGalaxy_Butler006", "AstroDome_Butler023",
                     "AstroDome_Butler011", "AstroDome_Butler014",
                     "AstroDome_Butler017", "AstroGalaxy_Butler000")}  # ButlerStateStarPieceReaction
COMMON = {"Tico": ("Common_Tico000",), "TicoFat": ("Common_TicoFat000", "Common_TicoFat001", "InformationPlanet")}


def _arc(fsp, rel):
    return {n.lower(): b for n, b in B.rarc_files((Path(fsp) / rel).read_bytes())}


class Bmg:
    def __init__(self, fsp, lang=LANG):
        f = _arc(fsp, f"{lang}/MessageData/Message.arc")
        self.b = b = f["message.bmg"]
        self.index = dict(zip(B.bcsv_column(f["messageid.tbl"], "MessageId"), B.bcsv_column(f["messageid.tbl"], "Index")))
        blk, o = {}, 0x20
        for _ in range(struct.unpack_from(">I", b, 0xC)[0]):
            blk[b[o:o + 4]] = o
            o += struct.unpack_from(">I", b, o + 4)[0]
        self.inf, self.dat, flw = blk[b"INF1"], blk[b"DAT1"], blk[b"FLW1"]
        self.nmsg, self.isz = struct.unpack_from(">HH", b, self.inf + 8)
        nn, nb = struct.unpack_from(">HH", b, flw + 8)
        self.nodes = [struct.unpack_from(">BBHHH", b, flw + 0x10 + 8 * i) for i in range(nn)]
        self.branch = struct.unpack_from(f">{nb}H", b, flw + 0x10 + 8 * nn)

    def info(self, i):
        e = self.inf + 0x10 + self.isz * i
        return struct.unpack_from(">IHBBBBbb", self.b, e)

    def units(self, i):
        """The message as u16 units (tags re-encoded, see TALK)."""
        p = self.dat + 8 + self.info(i)[0]
        out = []
        while True:
            c = struct.unpack_from(">H", self.b, p)[0]
            if c == 0:
                return out
            if c == 0x1A:
                n = self.b[p + 2]
                group, typ = self.b[p + 3], struct.unpack_from(">H", self.b, p + 4)[0]
                par = self.b[p + 6:p + n]
                par += b"\0" * (len(par) & 1)
                out += [0x1A, group, typ, n - 6] + list(struct.unpack(f"<{len(par) // 2}H", par))
                p += n
                continue
            out.append(c)
            p += 2

    def text(self, label):
        """A message's plain text (tags left out), None if the label is missing."""
        if label not in self.index:
            return None
        u, k, s = self.units(self.index[label]), 0, ""
        while k < len(u):
            if u[k] == 0x1A:
                k += 4 + (u[k + 3] + 1) // 2
            else:
                s += chr(u[k])
                k += 1
        return s

    def chars(self, i):
        """Plain characters and picture ids of message i."""
        u, k, ch, pic = self.units(i), 0, set(), set()
        while k < len(u):
            if u[k] == 0x1A:
                if u[k + 1] == 3:
                    pic.add(u[k + 2])
                k += 4 + (u[k + 3] + 1) // 2
            else:
                ch.add(u[k])
                k += 1
        return ch, pic

    def find_node(self, label):
        """MessageData::findNode: the message node of that message, else None."""
        mi = self.index.get(label)
        return next((k for k, n in enumerate(self.nodes) if n[0] == 1 and n[2] == mi), None) if mi is not None else None


def talk_chunk(fsp, ctls, lang=LANG):
    """ctls: [(label, row or None)] -> TALK chunk payload (labels missing from the BMG are left out)."""
    m = Bmg(fsp, lang)
    node_id, msg_id, nodes, msgs, entries = {}, {}, [], [], []

    def msg(i):
        if i not in msg_id:
            msg_id[i] = len(msgs)
            msgs.append(i)
        return msg_id[i]

    def node(k):  # reachable nodes, compact ids (FLW1 order kept per walk)
        if k is None or k == 0xFFFF or k >= len(m.nodes):
            return 0xFFFF
        if k in node_id:
            return node_id[k]
        node_id[k] = len(nodes)
        nodes.append(None)
        t, g, ix, a, b = m.nodes[k]
        if t == 1:
            rec = (t, g, msg(ix), node(a), 0xFFFF, 0xFFFF)
        elif t == 2:
            rec = (t, g, ix, a, node(m.branch[b]), node(m.branch[b + 1]))
        elif t == 3:
            rec = (t, g, ix, a, node(m.branch[ix]) if ix < len(m.branch) else 0xFFFF, b)
        else:
            rec = (t, g, ix, 0xFFFF, 0xFFFF, 0xFFFF)
        nodes[node_id[k]] = rec
        return node_id[k]

    for label, row in ctls:
        if label not in m.index:
            continue
        k = m.find_node(label)
        entries.append(struct.pack("<32sHHHH", label.encode()[:31], 0xFFFF if row is None else row, node(k),
                                   msg(m.index[label]), 0))
    text, offs = [], []
    for i in msgs:
        offs.append(len(text))
        text += m.units(i) + [0]
    recs = b"".join(struct.pack("<IHBBBBbb", o, *m.info(i)[1:]) for o, i in zip(offs, msgs))
    return (struct.pack("<HHHHI4x", len(entries), len(nodes), len(msgs), 0, 2 * len(text)) + b"".join(entries) +
            b"".join(struct.pack("<BBHHHHH", *n, 0) for n in nodes) + recs + struct.pack(f"<{len(text)}H", *text))


# ---------- nw4r ResFont ----------
def rfnt(b):
    """-> dict(line_feed, ascent, cell_w, cell_h, sheets [RGBA arrays], row, line, glyph(index) -> (left, glyph_w,
    char_w), code -> index)."""
    import tex
    finf = 0x10
    _, lf, alt, dl, dg, dc, enc, pg, pw, pm, height, width, ascent, _ = struct.unpack_from(">BbHbBbBIIIBBBB", b, finf + 8)
    cw, ch, base, maxw, ssize, snum, sfmt, srow, sline, sw, sh, simg = struct.unpack_from(">BBbBIHHHHHHI", b, pg)
    sheets = [tex.decode(b[simg + i * ssize:simg + (i + 1) * ssize], sfmt, sw, sh) for i in range(snum)]
    widths, p = {}, pw
    while p:
        i0, i1, nxt = struct.unpack_from(">HHI", b, p)
        for i in range(i0, i1 + 1):
            widths[i] = struct.unpack_from(">bBb", b, p + 8 + 3 * (i - i0))
        p = nxt
    cmap, p = {}, pm
    while p:
        c0, c1, meth, _, nxt = struct.unpack_from(">HHHHI", b, p)
        d = p + 12
        if meth == 0:
            off = struct.unpack_from(">H", b, d)[0]
            for c in range(c0, c1 + 1):
                cmap[c] = c - c0 + off
        elif meth == 1:
            for c in range(c0, c1 + 1):
                v = struct.unpack_from(">H", b, d + 2 * (c - c0))[0]
                if v != 0xFFFF:
                    cmap[c] = v
        else:
            n = struct.unpack_from(">H", b, d)[0]
            for k in range(n):
                c, v = struct.unpack_from(">HH", b, d + 2 + 4 * k)
                cmap[c] = v
        p = nxt
    return dict(line_feed=lf, ascent=ascent, cell_w=cw, cell_h=ch, sheets=sheets, row=srow, line=sline,
                widths=widths, default=(dl, dg, dc), cmap=cmap, fmt=sfmt)


def glyph_image(f, idx):
    per = f["row"] * f["line"]
    s, r = divmod(idx, per)
    y, x = divmod(r, f["row"])
    return f["sheets"][s][y * (f["cell_h"] + 1):y * (f["cell_h"] + 1) + f["cell_h"],
                          x * (f["cell_w"] + 1):x * (f["cell_w"] + 1) + f["cell_w"]]


def atlas(f, codes, size):
    """Shelf-pack the glyphs of `codes` (present in the font) into a size (w, h) image: -> (RGBA, [(code, u, v, w, h,
    left, glyph_w, char_w)])."""
    w, h = size
    img = np.zeros((h, w, 4), np.uint8)
    x = y = 0
    out = []
    for c in sorted(codes):
        if c not in f["cmap"]:
            continue
        i = f["cmap"][c]
        left, gw, cw = f["widths"].get(i, f["default"])
        g = glyph_image(f, i)[:, :max(gw, 1)]
        if x + g.shape[1] > w:
            x, y = 0, y + f["cell_h"] + 1
        assert y + g.shape[0] <= h, "font atlas full"
        img[y:y + g.shape[0], x:x + g.shape[1]] = g
        out.append((c, x, y, g.shape[1], g.shape[0], left, gw, cw))
        x += g.shape[1] + 1
    return img, out


def font_chunk(fsp, lang=LANG):
    """-> (FONT payload with tex indices relative to the first returned texture, [texture dicts]): every character
    and picture of the language's messages."""
    import tex
    f = _arc(fsp, "LayoutData/Font.arc")
    text, pic = rfnt(f["messagefont26.brfnt"]), rfnt(f["picturefont.brfnt"])
    m = Bmg(fsp, lang)
    chars, pics = set(), set()
    for i in range(m.nmsg):
        c, p = m.chars(i)
        chars |= c
        pics |= p
    chars |= set(range(0x20, 0x7F))
    timg, tg = atlas(text, chars, (256, 256))
    import lyt  # the menus' picture text boxes (tools/lyt.py)
    icons = {p + 0x30 for p in pics} | lyt.picture_chars(fsp)
    pimg, pg = atlas(pic, icons, (256, 256))
    pg = [(g[0] - 0x30,) + tuple(g[1:]) for g in pg]
    # NumberFont's digits (the HUD counters' text boxes) under the pictures, codes 0x1000 + character
    num = rfnt(f["numberfont.brfnt"])
    y0 = max(g[2] + g[4] for g in pg) + 2 if pg else 0
    nimg, ng = atlas(num, set(range(0x30, 0x3A)), (256, 256 - y0))
    pimg[y0:] = np.maximum(pimg[y0:], nimg)
    pg += [(0x1000 + g[0], g[1], g[2] + y0) + tuple(g[3:]) for g in ng]
    # the text atlas: PSMT8, CLUT white with alpha = I4 intensity (16 levels), CSM1 order
    idx = (timg[:, :, 0] >> 4).astype(np.uint8)
    pal = np.zeros((256, 4), np.uint32)
    pal[:16, :3] = 255
    pal[:16, 3] = [(k * 17 * 128 + 127) // 255 for k in range(16)]
    clut = (pal[:, 0] | pal[:, 1] << 8 | pal[:, 2] << 16 | pal[:, 3] << 24).astype("<u4")
    clut = clut[[i ^ ((i >> 3 ^ i >> 4) & 1) * 0x18 for i in range(256)]]
    ttex = dict(w=256, h=256, psm=tex.PSMT8, clamp_s=1, clamp_t=1, alpha=2, pix=idx.tobytes(), clut=clut.tobytes(),
                u_scale=1.0, v_scale=1.0)
    ptex = tex.to_ps2(pimg, 0, 0, hi=True)
    glyphs = b"".join(struct.pack("<HHHBBbBbx", *g) for g in tg + pg)
    glyphs += b"\0" * (-len(glyphs) % 4)
    lay, ltex, _ = layout.section(fsp, 2)  # the talk balloons' layouts (tools/layout.py), their textures after the font's
    head = struct.pack("<HHHHbBBBI", len(tg), len(pg), 0, 1, text["line_feed"], text["ascent"], text["cell_h"],
                       pic["cell_h"], 16 + len(glyphs))
    return head + glyphs + lay, [ttex, ptex] + ltex, (timg, pimg)


if __name__ == "__main__":  # python tools/msg.py data/fs/files: the atlases as PNGs in out/ (to look at)
    import sys
    from PIL import Image
    payload, texs, (t, p) = font_chunk(sys.argv[1])
    Image.fromarray(t, "RGBA").save("out/font_text.png")
    Image.fromarray(p, "RGBA").save("out/font_icons.png")
    print(len(payload), "bytes FONT")
