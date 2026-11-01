"""Runtime 2D layouts (nw4r lyt BRLYT + BRLAN + TPL, Petari's LayoutActor) for the engine's lyt.c.

Unlike layout.py (flat quads baked at an animation's end), the pane tree and every animation go out as they are and
the engine plays them (LayoutManager::startAnim / startPaneAnim). Output: a blob (slice.blob_of) with a LYTR chunk, a
TEXT chunk (the layouts' textures, PS2 format as slice.py's) and MSGS (the menus' messages), out/packs/menus.bin.

LYTR (little endian): u32 nlayouts, u32 pad[3], nlayouts x u32 offset (from the payload); per layout:
  Layout {char name[24]; u16 npanes, nmats, nanims, ntex; u16 tex0 (its first texture in the TEXT chunk), u16 nstr;
          u32 panes, mats, anims, strings (offsets from the layout)} (52 B)
  Pane   {char name[16]; s16 parent, mat; u8 kind (0 pan 1 pic 2 wnd 3 txt 4 bnd), flags (bit 0 visible, bit 1
          influenced alpha), origin (0..8: x left / centre / right, y top / centre / bottom), alpha;
          float t[3], r[3], s[2], size[2]; u8 vcol[4][4] (top left, top right, bottom left, bottom right RGBA);
          float uv[8] (the same corners' texture coordinates);
          window: float inflate[4] (left right top bottom); s16 frame_mat[4]; u8 nframes, flip[4] (0 none 1 h 2 v 3 180),
          pad[3];
          text: float font[2] (size x y), space, line; u8 top[4], bottom[4]; u16 str, len (its Layout_ message label,
          ASCII in strings: none, it starts empty); u8 pos (0..8 as origin), align, font (0 message 1 number
          2 picture), pad} (176 B)
  Mat    {char name[20]; s16 tex[2] (layout textures, -1 none); u8 wrap[4] (s t of each: 0 clamp 1 repeat 2 mirror);
          float srt[5] (tx ty rotation sx sy, texture SRT 0); u8 c0[4], c1[4]; u8 flags (bit 0 the colours baked into
          its texture: lerp(c0, c1, texel) not to be applied again), pad[3]; u16 tw, th (the texture's own size: a window's corners)} (64 B)
  Anim   {char name[24]; u16 frames; u8 loop, pad; u16 ntracks, ntexmap; u32 tracks, texmap (offsets from the layout)}
          (40 B); texmap: u16 layout texture per BRLAN texture name (RLTP values index it)
  Track  {s16 target (pane or material index); u8 is_mat, tag (0 RLPA 1 RLVC 2 RLVI 3 RLMC 4 RLTS 5 RLTP), index (the
          material's SRT / texture map), sub (the curve's target: RLPA 0..9 tx ty tz rx ry rz sx sy w h, RLVC 0..15
          vertex colours, 16 alpha, RLMC 0..3 material colour, 4..7 c0, 8..11 c1, RLTS 0..4 tx ty r sx sy), ctype (1
          step, 2 Hermite), pad; u16 nkeys, pad; u32 keys (offset from the layout: nkeys x {float frame, value, slope})}
          (16 B)
ponytail: one texture per material (a second one, a mask, is left out), vertex colours drawn as their mean, the
material colour and TEV stages past c0 / c1 not applied, windows with one frame texture (mirrored round) or none,
pane rotations other than z only as 180 degree flips.
"""
import struct
import sys
from pathlib import Path

import numpy as np

import budget as B
import tex
from layout import tpl

u16 = lambda b, o: struct.unpack_from(">H", b, o)[0]
u32 = lambda b, o: struct.unpack_from(">I", b, o)[0]
f32 = lambda b, o: struct.unpack_from(">f", b, o)[0]
cstr = lambda b, o, n: b[o:o + n].split(b"\0")[0].decode("ascii", "replace")

KINDS = {b"pan1": 0, b"pic1": 1, b"wnd1": 2, b"txt1": 3, b"bnd1": 4}
TAGS = {b"RLPA": 0, b"RLVC": 1, b"RLVI": 2, b"RLMC": 3, b"RLTS": 4, b"RLTP": 5}


def parse_brlyt(b):
    """-> (textures, fonts, materials, panes); panes in file order with their parent's index."""
    texs, fonts, mats, panes, stack, last = [], [], [], [], [], None
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind in (b"txl1", b"fnl1"):
            names = [cstr(b, o + 12 + u32(b, o + 12 + 8 * i), 64) for i in range(u16(b, o + 8))]
            (texs if kind == b"txl1" else fonts).extend(names)
        elif kind == b"mat1":
            for i in range(u16(b, o + 8)):
                mo = o + u32(b, o + 12 + 4 * i)
                fl = u32(b, mo + 0x3C)
                nt, ns = fl & 0xF, fl >> 4 & 0xF
                maps = [(u16(b, mo + 0x40 + 4 * k), b[mo + 0x42 + 4 * k], b[mo + 0x43 + 4 * k]) for k in range(nt)]
                so = mo + 0x40 + 4 * nt
                srts = [struct.unpack_from(">5f", b, so + 20 * k) for k in range(ns)]
                # the blend mode last (nw4r lyt MaterialResourceNum: coord gens, channel control, material colour,
                # TEV swap, indirect SRTs / orders, TEV stages, alpha compare before it): GX_BM_BLEND with
                # GX_BL_ONE as the destination adds (TitleLogo's PicBloomA / B, PicFlash, PicLogoShine)
                q = so + 20 * ns + 4 * (fl >> 8 & 0xF) + 4 * (fl >> 25 & 1) + 4 * (fl >> 27 & 1) + 4 * (fl >> 12 & 1)
                q += 20 * (fl >> 13 & 3) + 4 * (fl >> 15 & 7) + 16 * (fl >> 18 & 0x1F) + 4 * (fl >> 23 & 1)
                add = bool(fl >> 24 & 1) and b[q] == 1 and b[q + 2] == 1
                mats.append(dict(name=cstr(b, mo, 20).strip(),
                                 c0=struct.unpack_from(">4h", b, mo + 0x14), c1=struct.unpack_from(">4h", b, mo + 0x1C),
                                 maps=maps, srt=srts[0] if srts else (0, 0, 0, 1, 1), add=add))
        elif kind in KINDS:
            p = dict(kind=KINDS[kind], name=cstr(b, o + 12, 16), flags=b[o + 8], origin=b[o + 9], alpha=b[o + 10],
                     ud=cstr(b, o + 0x1C, 8),
                     t=struct.unpack_from(">3f", b, o + 0x24), r=struct.unpack_from(">3f", b, o + 0x30),
                     s=struct.unpack_from(">2f", b, o + 0x3C), size=struct.unpack_from(">2f", b, o + 0x44),
                     parent=stack[-1] if stack else -1, mat=-1, vcol=[(255, 255, 255, 255)] * 4, uv=(0, 0, 1, 0, 0, 1, 1, 1),
                     wnd=None, txt=None)
            if kind == b"pic1":
                p["vcol"] = [struct.unpack_from(">4B", b, o + 0x4C + 4 * k) for k in range(4)]
                p["mat"] = u16(b, o + 0x5C)
                if b[o + 0x5E]:
                    p["uv"] = struct.unpack_from(">8f", b, o + 0x60)
            elif kind == b"wnd1":
                co, fo = o + u32(b, o + 0x60), o + u32(b, o + 0x64)
                p["vcol"] = [struct.unpack_from(">4B", b, co + 4 * k) for k in range(4)]
                p["mat"] = u16(b, co + 16)
                if b[co + 18]:
                    p["uv"] = struct.unpack_from(">8f", b, co + 20)
                frames = [(u16(b, o + u32(b, fo + 4 * k)), b[o + u32(b, fo + 4 * k) + 2]) for k in range(b[o + 0x5C])]
                p["wnd"] = dict(inflate=struct.unpack_from(">4f", b, o + 0x4C), frames=frames[:4])
            elif kind == b"txt1":
                so, slen = u32(b, o + 0x58), u16(b, o + 0x4E)
                text = [u16(b, o + so + 2 * k) for k in range(slen // 2)] if so and slen else []
                while text and text[-1] == 0:
                    text.pop()
                p["mat"] = u16(b, o + 0x50)
                p["txt"] = dict(font=u16(b, o + 0x52), pos=b[o + 0x54], align=b[o + 0x55],
                                top=struct.unpack_from(">4B", b, o + 0x5C), bottom=struct.unpack_from(">4B", b, o + 0x60),
                                size=struct.unpack_from(">2f", b, o + 0x64), space=f32(b, o + 0x6C),
                                line=f32(b, o + 0x70), text=text)
            panes.append(p)
            last = len(panes) - 1
        elif kind == b"pas1":
            stack.append(last)
        elif kind == b"pae1":
            stack.pop()
        o += size
    return texs, fonts, mats, panes


def parse_brlan(b):
    """-> dict(frames, loop, textures, entries=[(name, is_mat, [(tag, index, target, ctype, keys)])]); keys as
    (frame, value, slope) (step keys: slope 0)."""
    out = dict(frames=0, loop=0, textures=[], entries=[])
    o = u16(b, 0xC)
    for _ in range(u16(b, 0xE)):
        kind, size = b[o:o + 4], u32(b, o + 4)
        if kind == b"pai1":
            out["frames"], out["loop"] = u16(b, o + 8), b[o + 10]
            ntex, nent, eoff = u16(b, o + 12), u16(b, o + 14), u32(b, o + 16)
            out["textures"] = [cstr(b, o + 20 + u32(b, o + 20 + 4 * i), 64) for i in range(ntex)]
            for e in range(nent):
                eo = o + u32(b, o + eoff + 4 * e)
                name, ntag, is_mat = cstr(b, eo, 20), b[eo + 20], b[eo + 21]
                tracks = []
                for t in range(ntag):
                    to = eo + u32(b, eo + 24 + 4 * t)
                    tag = b[to:to + 4]
                    if tag not in TAGS:
                        continue
                    for i in range(b[to + 4]):
                        io = to + u32(b, to + 8 + 4 * i)
                        index, target, ctype, nk = b[io], b[io + 1], b[io + 2], u16(b, io + 4)
                        ko = io + u32(b, io + 8)
                        if ctype == 2:
                            keys = [struct.unpack_from(">3f", b, ko + 12 * k) for k in range(nk)]
                        else:
                            keys = [(f32(b, ko + 8 * k), float(u16(b, ko + 8 * k + 4)), 0.0) for k in range(nk)]
                        tracks.append((TAGS[tag], index, target, ctype, keys))
                out["entries"].append((name, is_mat, tracks))
        o += size
    return out


def text_chunk(texts):
    """the TEXT chunk of PS2 textures (tex.to_ps2 dicts): the same layout as slice.py assets()'s"""
    from slice import chunk, lod_k
    head, data = bytearray(), bytearray()
    base = 16 + 32 * len(texts)
    for tx in texts:
        pix = base + len(data)
        for lv in [tx["pix"]] + tx.get("mips", []):
            data += lv + b"\0" * (-len(lv) % 16)
        size = base + len(data) - pix
        clut = base + len(data)
        data += tx["clut"]
        head += struct.pack("<HHBBBBIIIBBBxh6x", tx["w"], tx["h"], tx["psm"], tx["clamp_s"], tx["clamp_t"], tx["alpha"],
                            pix, size, clut, tx["w"].bit_length() - 1, tx["h"].bit_length() - 1,
                            len(tx.get("mips", [])), lod_k(tx.get("lod", (0, 0))))
    return chunk(b"TEXT", struct.pack("<I12x", len(texts)) + bytes(head) + bytes(data))


def font_kind(fonts, i):
    """a text box's font: 0 MessageFont26 (and MenuFont64), 1 NumberFont, 2 PictureFont, 3 CinemaFont26 (subs.bin's
    FNT2; MessageFont26 where a blob has none)"""
    n = fonts[i].lower() if i < len(fonts) else ""
    return 1 if n.startswith("numberfont") else 2 if n.startswith("picturefont") else 3 if n.startswith("cinemafont") else 0


def picture_chars(fsp, names=None):
    """the PictureFont characters the layouts' picture text boxes show (their Layout_ messages), and the ones the
    code sets (PauseMenu's star piece box: 0x3B / 0x57, 0x40) -> character codes (msg.font_chunk adds the icons)"""
    import msg
    m = msg.Bmg(fsp)
    out = {0x3B, 0x40, 0x57}
    for n in names or COMMON:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(fsp) / "LayoutData" / f"{n}.arc").read_bytes())}
        _, fonts, _, panes = parse_brlyt(files[n.lower() + ".brlyt"])
        for i, p in enumerate(panes):
            if not p["txt"] or font_kind(fonts, p["txt"]["font"]) != 2:
                continue
            k, label = i, ""
            while k >= 0 and not label:
                label = panes[k]["ud"] and f"Layout_{n}{panes[k]['ud']}"
                k = panes[k]["parent"]
            if label in m.index:
                out |= {c for c in m.units(m.index[label]) if 0x20 <= c < 0x100}
    return out


def bti(b):
    """A BTI image -> RGBA"""
    fmt, w, h, off = b[0], struct.unpack_from(">H", b, 2)[0], struct.unpack_from(">H", b, 4)[0], struct.unpack_from(">I", b, 0x1C)[0]
    return tex.decode(b[off:], fmt, w, h).copy()


def convert(files, name, texts, extra=None):
    """one layout (its RARC's files by lower-case base name) -> its Layout record and body; its textures appended to
    `texts` (PS2 dicts). extra: {material name: BTI bytes} added as materials of their own (MR::replacePaneTexture's
    textures: GalaxyInfoLayoutSetter's ObjectData/GalaxyInfoTexture.arc)"""
    texs, fonts, mats, panes = parse_brlyt(files[name.lower() + ".brlyt"])
    files = dict(files)
    for mn, b in (extra or {}).items():
        files["@bti:" + mn.lower()] = b
        mats.append(dict(name=mn, c0=(0, 0, 0, 0), c1=(255, 255, 255, 255), maps=[(len(texs), 0, 0)], srt=(0, 0, 0, 1, 1)))
        texs.append("@bti:" + mn)
    anims = sorted((k[:-6], parse_brlan(v)) for k, v in files.items() if k.endswith(".brlan"))
    pidx = {p["name"]: i for i, p in enumerate(panes)}
    midx = {m["name"]: i for i, m in enumerate(mats)}
    # materials whose colours an animation moves keep them live (not baked into the texture)
    live = {e[0] for _, a in anims for e in a["entries"] if e[1] and any(t[0] == 3 and 4 <= t[2] < 12 for t in e[2])}
    images, img_ix = [], {}

    def image(texname, c0, c1, wrap, mask=None, add=False):
        key = (texname, c0, c1, wrap, mask, add)
        if key not in img_ix:
            f = files.get(texname.lower())
            if f is None:
                return -1
            rgba = (bti(f) if texname.startswith("@bti:") else tpl(f)).astype(np.float32) / 255
            if mask and mask.lower() in files:  # a second map as its mask (TitleLogo's PicLogoGalaxy: the space through
                # the letters): baked at the mask's size, the first one's colour over it, the mask's alpha.
                # ponytail: the first map's scroll (its SRT) is lost
                mk = tpl(files[mask.lower()]).astype(np.float32) / 255
                h, w = mk.shape[:2]
                ys = (np.arange(h) * rgba.shape[0] // h)[:, None]
                xs = (np.arange(w) * rgba.shape[1] // w)[None, :]
                rgba = rgba[ys, xs].copy()
                rgba[..., 3] *= mk[..., 3] if mk[..., 3].min() < 1 else mk[..., :3].mean(-1)
            if c0 is not None:  # lyt's default TEV for one texture: lerp(c0, c1, texel)
                a, b_ = np.array(c0, np.float32) / 255, np.array(c1, np.float32) / 255
                ta = rgba[..., 3].copy()
                rgba = np.clip(a + (b_ - a) * rgba, 0, 1)
                if add:  # added (Cs x As + Cd): the texture's own alpha weighs it (both colours' alpha are 0)
                    rgba[..., 3] = ta
            img_ix[key] = len(images)
            images.append(((rgba * 255 + 0.5).astype(np.uint8), wrap))
        return img_ix[key]

    mrecs = []
    for m in mats:
        texi, wraps, baked, tsz = [-1, -1], [0, 0, 0, 0], 0, (0, 0)
        for k, (ti, ws, wt) in enumerate(m["maps"][:1]):
            plain = m["c0"] == (0, 0, 0, 0) and m["c1"] == (255, 255, 255, 255)
            bake = not plain and m["name"] not in live
            c0 = tuple(max(0, min(255, v)) for v in m["c0"])
            c1 = tuple(max(0, min(255, v)) for v in m["c1"])
            mask = texs[m["maps"][1][0]] if len(m["maps"]) > 1 and m["maps"][1][0] < len(texs) else None
            texi[k] = image(texs[ti], c0 if bake else None, c1 if bake else None, (ws, wt), mask, m.get("add", False)) if ti < len(texs) else -1
            wraps[2 * k:2 * k + 2] = [ws, wt]
            if ti < len(texs) and texs[ti].lower() in files and not texs[ti].startswith("@bti:"):
                tsz = tpl(files[(mask if mask and mask.lower() in files else texs[ti]).lower()]).shape[1::-1]
            if mask:
                wraps[2 * k:2 * k + 2] = [0, 0]  # the mask's clamp
            baked = 1 if bake else baked
        c0 = bytes(max(0, min(255, v)) for v in m["c0"])
        c1 = bytes(max(0, min(255, v)) for v in m["c1"])
        srt = (0, 0, 0, 1, 1) if len(m["maps"]) > 1 else m["srt"]  # a masked one: the mask's place, not the scroll
        mrecs.append(struct.pack("<20s2h4B5f4s4sBB2xHH", m["name"].encode()[:20], *texi, *wraps, *srt, c0, c1, baked,
                                 m.get("add", False), *tsz))
    strings = []
    precs = []
    for p in panes:
        rec = struct.pack("<16shhBBBB3f3f2f2f", p["name"].encode()[:16], p["parent"], p["mat"], p["kind"], p["flags"],
                          p["origin"], p["alpha"], *p["t"], *p["r"], *p["s"], *p["size"])
        rec += b"".join(bytes(c) for c in p["vcol"]) + struct.pack("<8f", *p["uv"])
        w = p["wnd"] or dict(inflate=(0, 0, 0, 0), frames=[])
        fr = (w["frames"] + [(0xFFFF, 0)] * 4)[:4]
        rec += struct.pack("<4f4hB4B3x", *w["inflate"], *[(-1 if m_ == 0xFFFF else m_) for m_, _ in fr],
                           len(w["frames"]), *[f for _, f in fr])
        t = p["txt"]
        if t:  # LayoutManager::initTextBoxRecursive: the message Layout_<layout><user data> of the nearest pane up
            # the tree that has user data; none: the box starts empty
            k, label = panes.index(p), ""
            while k >= 0 and not label:
                label = panes[k]["ud"] and f"Layout_{name}{panes[k]['ud']}"
                k = panes[k]["parent"]
            st = len(strings)
            strings += [ord(c) for c in label]
            rec += struct.pack("<2f2f4s4sHHBBBx", *t["size"], t["space"], t["line"], bytes(t["top"]), bytes(t["bottom"]),
                               st, len(label), t["pos"], t["align"], font_kind(fonts, t["font"]))
        else:
            rec += b"\0" * 32
        assert len(rec) == 176, len(rec)
        precs.append(rec)
    arecs, abody = [], []
    for an, a in anims:
        wrap = (0, 0)  # its patterns wrap as the material they go on (a mirrored one doubled: MiiIcon's faces)
        for ename, is_mat, trs in a["entries"]:
            if is_mat and ename in midx and mats[midx[ename]]["maps"] and any(t[0] == 5 for t in trs):
                wrap = tuple(mats[midx[ename]]["maps"][0][1:3])
                break
        tmap = [image(tn, None, None, wrap) for tn in a["textures"]]
        tracks, keys = [], []
        for ename, is_mat, trs in a["entries"]:
            target = (midx if is_mat else pidx).get(ename)
            if target is None:
                continue
            for tag, index, sub, ctype, ks in trs:
                tracks.append((target, is_mat, tag, index, sub, ctype, len(ks), len(keys)))
                keys += ks
        arecs.append((an, a["frames"], a["loop"], tracks, keys, tmap))
    # body: panes, mats, anims (records, then each one's tracks, texmap, keys), strings
    head_size = 52
    out = bytearray()
    off_panes = head_size
    out += b"".join(precs)
    off_mats = head_size + len(out)
    out += b"".join(mrecs)
    off_anims = head_size + len(out)
    arec_size = 40 * len(arecs)
    body = bytearray()
    recs = bytearray()
    base = off_anims + arec_size
    for an, frames, loop, tracks, keys, tmap in arecs:
        t_off = base + len(body)
        kbase = t_off + 16 * len(tracks)
        tm_off = kbase + 12 * len(keys)
        trb = b"".join(struct.pack("<hBBBBBxHxxI", tg, im, tag, idx, sub, ct, nk, kbase + 12 * k0)
                       for tg, im, tag, idx, sub, ct, nk, k0 in tracks)
        kb = b"".join(struct.pack("<3f", *k) for k in keys)
        tmb = struct.pack(f"<{len(tmap)}h", *tmap) if tmap else b""
        body += trb + kb + tmb + b"\0" * (-(len(trb) + len(kb) + len(tmb)) % 4)
        recs += struct.pack("<24sHBxHHII", an.encode()[:24], frames, loop, len(tracks), len(tmap), t_off, tm_off)
    out += recs + body
    off_str = head_size + len(out)
    out += struct.pack(f"<{len(strings)}H", *strings) if strings else b""
    out += b"\0" * (-len(out) % 16)
    tex0 = len(texts)
    for rgba, (ws, wt) in images:
        texts.append(tex.to_ps2(rgba, ws, wt, hi=True))
    head = struct.pack("<24sHHHHHHIIII", name.encode()[:24], len(panes), len(mats), len(arecs), len(images), tex0,
                       len(strings), off_panes, off_mats, off_anims, off_str)
    return head + bytes(out)


# the layouts every scene may show (menus.bin, loaded with each scene)
COMMON = ("PauseMenu", "SysInfoWindow", "SysInfoWindowMini", "GameOver", "TooBad", "StageTitle", "IconAButton",
          "BackButton", "ScenarioSelect", "WipeKoopa", "ResultWindow", "CinemaFrame", "FlyMeter", "AirMeter",  # MarioSubMeter
          "PointerGuidance",  # StarPointerGuidance
          "SelectButton")  # YesNoLayout (the talks' yes / no)
# the dome's galaxy select's (AstroDomeGalaxySelector): dome.bin, loaded in the AstroDome stages only; GalaxyInfo with
# the galaxies' pictures (GalaxyInfoTexture.arc) as materials named by galaxy (20 characters)
DOME = ("GalaxySelect", "GalaxyInfo", "GalaxyNamePlate")
# the Observatory's (GalaxyMapController, PowerStarList: ButlerMap's map): map.bin, in the AstroGalaxy stages only;
MAP = ("MapGrandGalaxy", "MapGalaxyIcon", "MapDomeIcon", "MapTitle", "MapGalaxyBg", "MapButton", "GalaxyDetail", "IconMario",
       "GalaxyNamePlate", "AllStarList", "PrologueStarSteward",
       "MapTicoIcon", "IconComet")  # GalaxyMapTicoIcon, GalaxyMapCometIcon  # RosettaMonologue (ロゼッタ状況説明デモ's picture book)


# the ending's (StaffRoll: StaffRollLine, StaffRollTitle, StaffRollPicture with StaffRollTexture.arc's pictures as
# materials named by their file, its InformationMessage): ending.bin, in the EpilogueDemoStage stage only
ENDING = ("StaffRoll", "StaffRollTitle", "StaffRollPicture", "InformationWindow")


def build_mappics(fsp):
    """mappics.bin: the map's GalaxyDetail pictures (ObjectData/GalaxyInfoTexture.arc), not resident: menu.c gm_pic
    reads the one shown when the detail opens. u32 n, slot, pad[2]; n x {char name[20] (the BTI's, lower case, cut to
    20); u32 off; its TEXT entry (32 B as text_chunk's, pix / clut offsets from its slot)}; slots from 4096 on, each
    `slot` bytes (whole sectors: the disc reads them)"""
    from slice import lod_k
    pics = [(n.rsplit("/", 1)[-1][:-4][:20].lower(), b) for n, b in
            B.rarc_files((Path(fsp) / "ObjectData" / "GalaxyInfoTexture.arc").read_bytes()) if n.lower().endswith(".bti")]
    slots = []
    for name, b in pics:
        tx = tex.to_ps2(np.ascontiguousarray(bti(b), np.uint8), 0, 0, hi=True)  # as convert's image() (no c0 / c1, clamped)
        data = bytearray()
        for lv in [tx["pix"]] + tx.get("mips", []):
            data += lv + b"\0" * (-len(lv) % 16)
        size, clut = len(data), len(data)
        data += tx["clut"]
        ent = struct.pack("<HHBBBBIIIBBBxh6x", tx["w"], tx["h"], tx["psm"], tx["clamp_s"], tx["clamp_t"], tx["alpha"],
                          0, size, clut, tx["w"].bit_length() - 1, tx["h"].bit_length() - 1, len(tx.get("mips", [])),
                          lod_k(tx.get("lod", (0, 0))))
        slots.append((name, ent, bytes(data)))
    slot = max(len(d) for _, _, d in slots)
    slot += -slot % 2048
    head = struct.pack("<II8x", len(slots), slot)
    head += b"".join(struct.pack("<20sI", n.encode(), 4096 + k * slot) + e for k, (n, e, _) in enumerate(slots))
    assert len(head) <= 4096
    return head.ljust(4096, b"\0") + b"".join(d.ljust(slot, b"\0") for _, _, d in slots)


# Luigi's letter (LuigiLetter: the pause menu's and KinopioAstro's) with each LuigiPicture<galaxy>.arc picture as a
# material named by its galaxy (20 characters): letter.bin, loaded only in a scene where the letter can show
# (main.c: getLuigiLetterGalaxyName / ...ForNPC)
LETTER = ("LuigiLetter",)


def build_letter(fsp):
    pics = {}
    for a in sorted((Path(fsp) / "ObjectData").glob("LuigiPicture*.arc")):
        for n, b in B.rarc_files(a.read_bytes()):
            if n.lower().endswith(".bti"):
                pics[a.stem[len("LuigiPicture"):][:20]] = b
    return build_dome(fsp, LETTER, "LuigiLetter", pics)


def build_ending(fsp):
    pics = {n.rsplit("/", 1)[-1][:-4][:20]: b for n, b in B.rarc_files((Path(fsp) / "ObjectData" / "StaffRollTexture.arc").read_bytes())
            if n.lower().endswith(".bti")}
    return build_dome(fsp, ENDING, "StaffRollPicture", pics)


# the movies' (MoviePlayingSequence): subs.bin, loaded by main.c while the movies play (no scene then): MovieSubtitles'
# CinemaSuper with its messages (MovieSubtitlesDataTable's Layout_StoryDemo*) and only their characters of
# MessageFont26 (FONT, its atlas the one texture); MRUM: DemoPadRumbler's MoviePadRumble<Name>.arc (PadRumbleData.bcsv)
# as u32 n, pad[3]; n x {char movie[16]; s32 StartFrame; s32 pattern (WPadRumbleData's index)}
RUMBLE_NAMES = ("最強【長】", "最強", "強", "中", "弱", "微弱")


def build_subs(fsp):
    import msg
    from slice import blob_of, chunk
    labels = sorted(l for l in msg.Bmg(fsp).index if l.startswith("Layout_StoryDemo"))
    font, ftex = msg.font_subset_chunk(fsp, labels)
    font2, ftex2 = msg.font_subset_chunk(fsp, labels, font="cinemafont26.brfnt", tex=1)  # CinemaSuper's ShaText
    files = {k.lower().rsplit("/", 1)[-1]: v for k, v in B.rarc_files((Path(fsp) / "LayoutData" / "CinemaSuper.arc").read_bytes())}
    texts = list(ftex) + list(ftex2)  # the fonts' atlases first: FONT's tex index 0, FNT2's 1
    body = convert(files, "CinemaSuper", texts)
    lytr = struct.pack("<I12x", 1) + struct.pack("<I", 20) + body
    rum = []
    for p in sorted((Path(fsp) / "ObjectData").glob("MoviePadRumble*.arc")):
        b = next(v for k, v in B.rarc_files(p.read_bytes()) if k.lower().endswith("padrumbledata.bcsv"))
        for n, f in zip(B.bcsv_column(b, "PadRumbleName"), B.bcsv_column(b, "StartFrame")):
            rum.append(struct.pack("<16sii", p.stem[len("MoviePadRumble"):].encode(), f, RUMBLE_NAMES.index(n)))
    return blob_of([chunk(b"LYTR", lytr), text_chunk(texts), chunk(b"FONT", font), chunk(b"FNT2", font2),
                    msgs_chunk(fsp, ("Layout_StoryDemo",)),
                    chunk(b"MRUM", struct.pack("<I12x", len(rum)) + b"".join(rum))])


# the messages menus and screens look up by label (MR::getLayoutMessageDirect / getGameMessageDirect): MSGS chunk
MSG_PREFIXES = ("Layout_", "System_", "Select_", "NAND_", "PauseMenu_", "PlayerLeft", "ScenarioName_", "GalaxyName_",
                "GalaxyNameShort_", "GalaxyStage_", "GalaxyInfo_", "GalaxyMap_", "GalaxyNamePlate_", "GetStar_",
                "AllStarList_", "AstroName_", "CometName_", "RaceName_", "ConfirmEndGame", "Common_", "CONT_",
                "PointerGuidance", "BButtonGuidance", "DPDGuidance", "WiiMessage", "Picture_", "AstroDome_", "RosettaMonologue",
                "StaffList", "StaffRollInfo_", "InformationObserver")


def msgs_chunk(fsp, prefixes=MSG_PREFIXES):
    """MSGS: u32 n, pad[3]; n x {u32 label, text} (byte offsets from the payload; labels sorted, NUL ended; texts UTF-16
    with the BMG's tags, 0 ended)"""
    import msg
    from slice import chunk
    m = msg.Bmg(fsp)
    labels = sorted(l for l in m.index if l.startswith(prefixes))
    lpool, tpool, ents = bytearray(), [], []
    for l in labels:
        ents.append((len(lpool), len(tpool)))
        lpool += l.encode() + b"\0"
        tpool += m.units(m.index[l]) + [0]
    lbase = 16 + 8 * len(labels)
    lpool += b"\0" * (-len(lpool) % 2)
    tbase = lbase + len(lpool)
    body = b"".join(struct.pack("<II", lbase + lo, tbase + 2 * to) for lo, to in ents)
    return chunk(b"MSGS", struct.pack("<I12x", len(labels)) + body + bytes(lpool) + struct.pack(f"<{len(tpool)}H", *tpool))


def dol_tables(fsp):
    """cStoryProgressTable / cGalaxyInfoTable: BCSVs in main.dol's .rodata (Petari config/RMGE01: symbols.txt
    0x8053C620 size 0x1E0, 0x8053C800 size 0xD20)"""
    d = (Path(fsp).parent / "sys" / "main.dol").read_bytes()
    offs, addrs, sizes = (struct.unpack_from(">18I", d, k) for k in (0, 0x48, 0x90))

    def rd(addr, n):
        for o, a, s in zip(offs, addrs, sizes):
            if a <= addr < a + s:
                return d[o + addr - a:o + addr - a + n]
        raise KeyError(hex(addr))
    return rd(0x8053C620, 0x1E0), rd(0x8053C800, 0xD20)


def gdat_chunk(fsp):
    """GDAT (the game data the menus and the story read): u32 nstory, ngalaxy, nscen, pad;
    nstory x {char name[64] (UTF-8), u32 progress} (cStoryProgressTable);
    ngalaxy x {char name[32]; u8 power_star_num (to open), grand_no, pad[2]; char cond[3][40] (OpenCondition0..2);
               char map_pane[16]} (cGalaxyInfoTable, 172 B);
    nscen x {char galaxy[32]; u8 nscen, pad[3]; 8 x {u8 star_id, hidden, pad[2]; char comet[12]; u32 luigi_timer}}
            (every galaxy's ScenarioData.bcsv, by ScenarioNo, 196 B)"""
    from slice import chunk
    st, gi = dol_tables(fsp)
    col = B.bcsv_column
    story = b"".join(struct.pack("<64sI", n.encode()[:63], p)  # UTF-8: as gamedata.c's flag table literals
                     for n, p in zip(col(st, "name"), col(st, "progress")))
    names = col(gi, "name")
    gal = b""
    for k, n in enumerate(names):
        conds = [(col(gi, f"OpenCondition{c}") or [""] * len(names))[k] for c in range(3)]
        gal += struct.pack("<32sBB2x40s40s40s16s", n.encode()[:31], col(gi, "PowerStarNum")[k], col(gi, "GrandGalaxyNo")[k],
                           *[c.encode()[:39] for c in conds], col(gi, "MapPaneName")[k].encode()[:15])
    scen, nscen = b"", 0
    for g in sorted(p.name for p in (Path(fsp) / "StageData").iterdir()):
        arc = Path(fsp) / "StageData" / g / f"{g}Scenario.arc"
        if not arc.exists():
            continue
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in B.rarc_files(arc.read_bytes())}
        sd = files.get("scenariodata.bcsv")
        if not sd:
            continue
        nos = col(sd, "ScenarioNo")
        rows = {no: i for i, no in enumerate(nos)}
        rec = struct.pack("<32sB3x", g.encode()[:31], len(nos))
        for no in range(1, 9):
            i = rows.get(no)
            if i is None:
                rec += b"\0" * 20
                continue
            v = lambda f, dflt: (col(sd, f) or [dflt] * len(nos))[i]
            rec += struct.pack("<BB2x12sI", v("PowerStarId", 0), 1 if v("IsHidden", 0) else 0,
                               str(v("Comet", "")).encode()[:11], v("LuigiModeTimer", 0))
        scen += rec
        nscen += 1
    return chunk(b"GDAT", struct.pack("<4I", len(story) // 68, len(names), nscen, 0) + story + gal + scen)


def build(fsp, names=COMMON):
    from slice import blob_of, chunk
    texts, bodies = [], []
    for n in names:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(fsp) / "LayoutData" / f"{n}.arc").read_bytes())}
        bodies.append(convert(files, n, texts))
    offs, o = [], 16 + 4 * len(bodies)
    for bd in bodies:
        offs.append(o)
        o += len(bd)
    lytr = struct.pack("<I12x", len(bodies)) + struct.pack(f"<{len(offs)}I", *offs) + b"".join(bodies)
    return blob_of([chunk(b"LYTR", lytr), text_chunk(texts), msgs_chunk(fsp), gdat_chunk(fsp)]), len(texts)


# the 3D models the menus show (menus3d.bin, a blob of its own: its TEXT apart from menus.bin's): ScenarioSelectStar's
# PowerStar (skinned, its ScenarioAppear / ScenarioHide clips as "selectpowerstar/<clip>"), ScenarioSelectSky (its BTK)
# ScenarioSelectStar::appear setBvaFrameAndStop(status 0 ? 1 : 0): the coloured star (frame 0, slice.py VIS) and the
# empty one of a scenario not cleared ("#PowerStar": its last frame)
MODELS_3D = (("SelectPowerStar", "PowerStar"), ("SelectPowerStarEmpty", "PowerStar#PowerStar"),
             ("ScenarioSelectSky", "ScenarioSelectSky"))


def build_3d(fsp):
    import slice as SL
    for name, arc in MODELS_3D:
        if name.startswith("SelectPowerStar"):
            SL.AUTO_SKINNED[name] = arc.partition("#")[0]
    chunks = SL.assets(Path(fsp), list(MODELS_3D), {}, 0.99, {}, collision=False)
    chunks.append(SL.obj_anim_chunk(Path(fsp), ["SelectPowerStar", "ScenarioSelectSky"]))  # one set of clips for both
    return SL.blob_of(chunks)


# the title and file select's (FileSelector, TitleSequenceProduct): title.bin (layouts) and title3d.bin (FileSelectItem's
# planet and fellows, FileSelectSky), loaded in the FileSelect stage only
TITLE = ("TitleLogo", "PressStart", "FileSelect", "FileInfo", "FileNumber", "MiiSelect", "MiiIcon", "BrosButton")
TITLE_3D = (("FileSelectDataPlanet", "FileSelectDataPlanet"), ("FileSelectDataMario", "FileSelectDataMario"),
            ("FileSelectDataLuigi", "FileSelectDataLuigi"), ("FileSelectDataYoshi", "FileSelectDataYoshi"),
            ("FileSelectDataKinopio", "FileSelectDataKinopio"), ("FileSelectDataPeach", "FileSelectDataPeach"),
            ("CometNearOrbitSky", "CometNearOrbitSky"), ("MiniatureGalaxySelect", "MiniatureGalaxySelect"))


def build_dome(fsp, names=DOME, with_pics="GalaxyInfo", pics=None):
    from slice import blob_of, chunk
    if pics is None:
        pics = {n.rsplit("/", 1)[-1][:-4][:20]: b for n, b in B.rarc_files((Path(fsp) / "ObjectData" / "GalaxyInfoTexture.arc").read_bytes())
                if n.lower().endswith(".bti")}
    texts, bodies = [], []
    for n in names:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(fsp) / "LayoutData" / f"{n}.arc").read_bytes())}
        bodies.append(convert(files, n, texts, pics if n == with_pics else None))
    offs, o = [], 16 + 4 * len(bodies)
    for bd in bodies:
        offs.append(o)
        o += len(bd)
    lytr = struct.pack("<I12x", len(bodies)) + struct.pack(f"<{len(offs)}I", *offs) + b"".join(bodies)
    return blob_of([chunk(b"LYTR", lytr), text_chunk(texts)])


def build_title(fsp):
    from slice import blob_of, chunk
    import slice as SL
    texts, bodies = [], []
    for n in TITLE:
        files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
                 B.rarc_files((Path(fsp) / "LayoutData" / f"{n}.arc").read_bytes())}
        bodies.append(convert(files, n, texts))
    offs, o = [], 16 + 4 * len(bodies)
    for bd in bodies:
        offs.append(o)
        o += len(bd)
    lytr = struct.pack("<I12x", len(bodies)) + struct.pack(f"<{len(offs)}I", *offs) + b"".join(bodies)
    return blob_of([chunk(b"LYTR", lytr), text_chunk(texts)]), SL.blob_of(SL.assets(Path(fsp), list(TITLE_3D), {}, 0.99, {}, collision=False))


def selftest(fsp="data/fs/files"):
    """PauseMenu parses: its root, the button boxes, appear's tracks on its panes, a record per pane"""
    files = {k.lower().rsplit("/", 1)[-1]: v for k, v in
             B.rarc_files((Path(fsp) / "LayoutData" / "PauseMenu.arc").read_bytes())}
    _, _, mats, panes = parse_brlyt(files["pausemenu.brlyt"])
    names = {p["name"] for p in panes}
    assert panes[0]["name"] == "RootPane" and {"BoxButton1", "BoxButton2", "TxtStageTitle"} <= names
    a = parse_brlan(files["appear.brlan"])
    assert a["frames"] > 0 and any(e[0] in names for e in a["entries"])
    body = convert(files, "PauseMenu", [])
    off_panes, off_mats = struct.unpack_from("<II", body, 36)
    assert off_panes == 52 and off_mats == 52 + 176 * len(panes)


if __name__ == "__main__":  # python tools/lyt.py data/fs/files [out/packs/menus.bin]; --selftest
    if "--selftest" in sys.argv:
        selftest()
        sys.exit(print("lyt selftest ok"))
    fsp = sys.argv[1] if len(sys.argv) > 1 else "data/fs/files"
    dst = Path(sys.argv[2] if len(sys.argv) > 2 else "out/packs/menus.bin")
    blob, nt = build(fsp)
    dst.write_bytes(blob)
    d3 = dst.with_name("menus3d.bin")
    d3.write_bytes(build_3d(fsp))
    print(f"{d3}: {d3.stat().st_size / 1024:.0f} KB")
    print(f"{dst}: {len(blob) / 1024:.0f} KB, {len(COMMON)} layouts, {nt} textures")
    dst.with_name("dome.bin").write_bytes(build_dome(fsp))
    dst.with_name("map.bin").write_bytes(build_dome(fsp, MAP, None))  # GalaxyDetail's pictures apart: mappics.bin
    dst.with_name("mappics.bin").write_bytes(build_mappics(fsp))
    print(f"map.bin {dst.with_name('map.bin').stat().st_size / 1024:.0f} KB, "
          f"mappics.bin {dst.with_name('mappics.bin').stat().st_size / 1024:.0f} KB")
    print(f"dome.bin {dst.with_name('dome.bin').stat().st_size / 1024:.0f} KB")
    dst.with_name("letter.bin").write_bytes(build_letter(fsp))
    print(f"letter.bin {dst.with_name('letter.bin').stat().st_size / 1024:.0f} KB")
    dst.with_name("ending.bin").write_bytes(build_ending(fsp))
    print(f"ending.bin {dst.with_name('ending.bin').stat().st_size / 1024:.0f} KB")
    dst.with_name("subs.bin").write_bytes(build_subs(fsp))
    print(f"subs.bin {dst.with_name('subs.bin').stat().st_size / 1024:.0f} KB")
    t2, t3 = build_title(fsp)
    dst.with_name("title.bin").write_bytes(t2)
    dst.with_name("title3d.bin").write_bytes(t3)
    print(f"title.bin {len(t2) / 1024:.0f} KB, title3d.bin {len(t3) / 1024:.0f} KB")
