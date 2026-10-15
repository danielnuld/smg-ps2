"""JParticle (JPAC2-10) reader: ParticleData/Effect.arc -> particle resources, their blocks and textures.

Layouts from Petari: JPAResourceLoader.cpp (container), JPADynamicsBlock.hpp (BEM1), JPABaseShape.hpp (BSP1),
JPAExtraShape.hpp (ESP1), JPAFieldBlock (FLD1), TDB1 (texture ids). Effect names (AutoEffectList EffectName) are
the particlenames.bcsv rows "<name>NN".

usage: python tools/jpa.py data/fs/files [EffectName...]   (dump)
       python tools/jpa.py --selftest
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import budget as B  # noqa: E402


def blocks_of(j, o, nblk):
    out = []
    for _ in range(nblk):
        magic, size = j[o:o + 4].decode("latin-1"), struct.unpack_from(">I", j, o + 4)[0]
        out.append((magic, j[o:o + size]))
        o += size
    return out, o


def read_jpc(j):
    """-> ({usr idx: [(magic, block bytes)]}, [(texture name, BTI bytes)])"""
    assert j[:8] == b"JPAC2-10", j[:8]
    nres, ntex, texo = struct.unpack_from(">HHI", j, 8)
    res, o = {}, 0x10
    for _ in range(nres):
        usr, nblk = struct.unpack_from(">HH", j, o)
        res[usr], o = blocks_of(j, o + 8, nblk)
    texs, o = [], texo
    for _ in range(ntex):
        size = struct.unpack_from(">I", j, o + 4)[0]
        texs.append((j[o + 0x0C:o + 0x20].split(b"\0")[0].decode("latin-1"), j[o + 0x20:o + size]))
        o += size
    return res, texs


def load(fs):
    d = {n.lower(): b for n, b in B.rarc_files((Path(fs) / "ParticleData" / "Effect.arc").read_bytes())}
    res, texs = read_jpc(d["particles.jpc"])
    names = dict(zip(B.bcsv_column(d["particlenames.bcsv"], "name"), B.bcsv_column(d["particlenames.bcsv"], "id")))
    return res, texs, names, d["autoeffectlist.bcsv"]


def effect_ids(names, effect):
    """Resource ids of an effect: its "<effect>NN" particle names (several space-separated effects allowed)."""
    out = []
    for e in effect.split():
        out += [i for n, i in names.items() if n[:-2] == e and n[-2:].isdigit()]
    return sorted(out)


def bem1(b):
    f = struct.unpack_from(">I", b, 8)[0]
    v = struct.unpack_from(">3f3f3f13f", b, 0x10)
    rot = struct.unpack_from(">3h", b, 0x68)
    maxf, startf, life, vsize, div = struct.unpack_from(">hhhHH", b, 0x6E)
    return dict(flags=f, vol=f >> 8 & 7, scl=v[0:3], trs=v[3:6], dir=v[6:9], vel_omni=v[9], vel_axis=v[10],
                vel_rndm=v[11], vel_dir=v[12], spread=v[13], vel_ratio=v[14], rate=v[15], rate_rndm=v[16],
                life_rndm=v[17], sweep=v[18], min_rad=v[19], air=v[20], moment=v[21], rot=rot, max_frame=maxf,
                start_frame=startf, life=life, vol_size=vsize, div=div, rate_step=b[0x78])


def bsp1(b):
    f, prm_off, env_off, sx, sy, blend = struct.unpack_from(">IhhffH", b, 8)
    tex_flg, tex_anm_num, tex_idx, clr_flg, prm_keys, env_keys, clr_max = struct.unpack_from(">BBBBBBh", b, 0x1E)
    return dict(flags=f, type=f & 0xF, size=(sx, sy), blend=blend, ztest=b[0x1D], tex_idx=tex_idx, tex_flg=tex_flg,
                clr_flg=clr_flg, prm=tuple(b[0x26:0x2A]), env=tuple(b[0x2A:0x2E]), clr_max=clr_max,
                prm_anm=[(struct.unpack_from(">h", b, prm_off + 6 * k)[0], tuple(b[prm_off + 6 * k + 2:prm_off + 6 * k + 6]))
                         for k in range(prm_keys)] if clr_flg & 2 else [],
                tex_anm=list(b[0x34:0x34 + tex_anm_num]) if tex_flg & 1 else [])


def esp1(b):
    f = struct.unpack_from(">I", b, 8)[0]
    v = struct.unpack_from(">7f2h13f", b, 0x0C)
    return dict(flags=f, scale_in_t=v[0], scale_out_t=v[1], scale_in_x=v[2], scale_out_x=v[3], scale_in_y=v[4],
                scale_out_y=v[5], scale_rndm=v[6], alpha_in_t=v[9], alpha_out_t=v[10], alpha_in=v[11],
                alpha_base=v[12], alpha_out=v[13], rot_angle=v[17], rot_speed=v[19])


def describe(blocks):
    out = {}
    for magic, b in blocks:
        if magic == "BEM1":
            out["bem"] = bem1(b)
        elif magic == "BSP1":
            out["bsp"] = bsp1(b)
        elif magic == "ESP1":
            out["esp"] = esp1(b)
        elif magic == "TDB1":
            out["tdb"] = list(struct.unpack_from(f">{(len(b) - 8) // 2}H", b, 8))
        else:
            out.setdefault("other", []).append((magic, len(b)))
    return out


def fld1(b):
    f = struct.unpack_from(">I", b, 8)[0]
    v = struct.unpack_from(">3f3ff", b, 0x0C)
    return dict(type=f & 0xF, add=f >> 8 & 3, local=not (f >> 16 & 2), dir=v[3:6], mag=v[6])


# Effects the engine plays (fx.c FX_* in this order): (AutoEffectList GroupName, UniqueName)
FX = [("Coin", "CoinGet"), ("PurpleCoin", "CoinGet"), ("StarPiece", "StarPieceLight"), ("StarPiece", "StarPieceGet"),
      ("PowerStar", "Light"), ("SpinDriver", "SpinDriverLight"), ("SuperSpinDriver", "SuperSpinDriverLight"),
      ("PunchBox", "Break"), ("CrystalCageS", "Break"), ("CrystalCageM", "Break"), ("SpaceMine", "Explosion"),
      ("Kuribo", "DeathSmokeKuribo"), ("TakoHei", "Death"), ("QuestionCoin", "Get"), ("BlueChip", "Get"),
      ("YellowChip", "Get"), ("BlueChip", "Wait"), ("YellowChip", "Wait"), ("QuestionCoin", "Light"),
      ("LavaSteam", "Sign"), ("LavaSteam", "Steam"), ("Mario", "SpinLight"), ("Mario", "HipDropFlower"),
      ("Mario", "Damage"), ("LavaGeyser", "Sign"), ("PhantomCandlestand", "Fire"), ("PhantomCandlestand", "Extinguish"),
      ("TeresaMansionCandlestand", "Fire01"), ("TeresaMansionCandlestand", "Fire02"),
      ("TeresaMansionCandlestand", "Extinguish01"), ("CandlestandIceVolcano", "Fire"), ("CandlestandIceVolcano", "Ignition"),
      ("FountainBig", "FountainBig"), ("FountainBig", "FountainBigSign"), ("MarioFireBall", "Fire"), ("MarioFireBall", "Break"),
      ("KuriboMini", "Death"), ("Takobo", "TakoboDeath"), ("Takobo", "Hit"),
      ("Karipon", "Death"), ("Karipon", "SmokeLand"), ("Karipon", "Hit"), ("Karipon", "Touch"),
      ("PackunPetit", "Death"), ("PackunPetit", "DeathPress"), ("PackunPetitHead", "Death"),
      ("Birikyu", "Hit"), ("Birikyu", "Clash"),
      ("HomingKiller", "Explosion"), ("HomingKiller", "Shoot"), ("HomingKiller", "SandColumn"),
      ("CocoNut", "CocoNutBreak"), ("CocoNut", "SpinHitMark"),
      ("Rock", "LandAttrDefault"), ("Rock", "Touch"), ("WanwanRolling", "Land"), ("WanwanRolling", "Touch"),
      ("WanwanRolling", "MiniBreak"), ("NeedlePlant", "Break"), ("CollapsePlane", "Vanish"), ("WoodBoxBreak", "Break")]
# Placed effect objects (NameObjFactory's *EffectObj* classes: SnowS, LavaSparksS...): each plays its AutoEffectList group
# (the first row of GroupName = its name); FX_AMBIENT_FIRST + k in fx_tab.h, names in fx_ambient (game.c ambient_fx)
AMBIENT = ("AstroTorchLightBlue", "AstroTorchLightRed", "BattleShipExplosionMetal", "BattleShipExplosionRock",
           "BirdLouseL", "BirdLouseS", "DrainPipeBubbleA", "DrainPipeBubbleB", "EffectObjSample", "EffectTeresa",
           "EffectTicoL", "EffectTicoS", "FallGreenLeaf", "FallRedLeaf", "FireworksA", "ForestWaterfallL",
           "ForestWaterfallS", "GravityDust", "IceLayerBreak", "IcePlanetLight", "IcicleRockLight",
           "LavaHomeVolcanoFallingRock", "LavaSparksL", "LavaSparksS", "SandBreezeL", "SandBreezeS",
           "SandUpDownKGBaseEffect", "SandUpDownTowerTopEffect", "ShootingStarArea", "SnowS", "SpaceDustL",
           "SpaceDustS", "StarPieceClusterBreak", "Steam", "TwinFallLakeWaterFall", "UFOKinokoLandingBlackSmoke",
           "UFOSandObstacleAEffect", "UFOSandObstacleBEffect", "UFOSandObstacleCEffect", "WaterDropBottom",
           "WaterDropMiddle", "WaterDropTop", "WaterLayerBreak", "WaterfallL", "WaterfallS")
FX += [(n, None) for n in AMBIENT]
FX_RES = 7  # resources per effect at most (fx.c FxDef)


def colour_mode(bsp):
    """BSP1 colour / alpha TEV inputs (JPABaseShape st_ca, st_aa): flags >> 15 & 7, flags >> 18 & 1."""
    return bsp["flags"] >> 15 & 7, bsp["flags"] >> 18 & 1


def bake_texture(rgba, bsp):
    """The particle texture with its TEV colour inputs baked where they are constant (modes 2-4: prm / env mixes);
    modes 0, 1, 5 keep the texture (the engine's vertex colour carries prm for 1 and 5). -> (rgba, vertex rgb
    from prm: True / white: False)."""
    import numpy as np
    cm, am = colour_mode(bsp)
    t = rgba[..., :3].astype(np.float32) / 255
    prm, env = np.array(bsp["prm"][:3], np.float32) / 255, np.array(bsp["env"][:3], np.float32) / 255
    if cm == 2:
        c = prm * (1 - t) + t
    elif cm == 3:
        c = env * (1 - t) + prm * t
    elif cm == 4:
        c = np.minimum(t * prm + env, 1)
    elif cm == 5:
        c = np.ones_like(t)
    else:
        c = t
    out = rgba.copy()
    out[..., :3] = (c * 255 + 0.5).astype(np.uint8)
    if am == 1:
        out[..., 3] = 255
    return out, cm in (1, 5)


def decode_bti(bti):
    """JPA texture (BTI header: format, width, height, data offset at 0x1C) -> RGBA; I4 / I8 read their intensity
    as alpha too (GX TEXA)."""
    import tex
    fmt, w, h = bti[0], struct.unpack_from(">H", bti, 2)[0], struct.unpack_from(">H", bti, 4)[0]
    rgba = tex.decode(bti[struct.unpack_from(">I", bti, 0x1C)[0]:], fmt, w, h).copy()
    if fmt in (0, 1):
        rgba[..., 3] = rgba[..., 0]
    return rgba


def tile_texture(rgba, bti, bsp):
    """JPABaseShape getTilingS / T (BSP1 flags bits 25 / 26): the quad's coordinates run 0..2 on that axis (jpa_crd),
    wrapped by the BTI's mode (0x06 / 0x07: 0 clamp, 1 repeat, 2 mirror). The sprites here run 0..1 over a clamped
    texture, so the texture is laid out twice along each tiled axis."""
    import numpy as np
    for axis, bit, wrap in ((1, 25, bti[6]), (0, 26, bti[7])):
        if bsp["flags"] >> bit & 1:
            second = np.flip(rgba, axis) if wrap == 2 else rgba if wrap == 1 else np.repeat(
                np.take(rgba, [-1], axis), rgba.shape[axis], axis)
            rgba = np.concatenate((rgba, second), axis)
    return rgba


def pack(fs):
    """-> (PTCL payload, [tex.to_ps2 dicts]): the FX effects' resources (fx.c PtRes, FxDef) and their textures.
    PTCL: u32 nres, u32 nfx, s32 first WaterPlant texture (-1 none), u32 nauto; nfx x FxDef {u16 n, u16 res[7], f32 off[3],
    f32 pad}; nres x PtRes; nauto x the animation effects (anim_effects)."""
    import numpy as np
    import tex
    res, texs, names, auto = load(fs)
    rows = list(zip(B.bcsv_column(auto, "GroupName"), B.bcsv_column(auto, "UniqueName"),
                    B.bcsv_column(auto, "EffectName")))
    used, defs, out_tex, tex_key = [], [], [], {}

    def add_def(e):
        ids = [names[n] for n in e.split() if n in names] or effect_ids(names, e)
        idx = []
        for i in ids[:FX_RES]:
            if i not in used:
                used.append(i)
            idx.append(used.index(i))
        defs.append(struct.pack("<H7H4f", len(idx), *(idx + [0] * (FX_RES - len(idx))), 0, 0, 0, 0))
        return len(defs) - 1
    for g, u in FX:
        add_def(next((r[2] for r in rows if r[0] == g and (u is None or r[1] == u)), ""))
    autos = anim_effects(fs, auto, add_def)
    recs = []
    for i in used:
        d = describe(res[i])
        bem, bsp, esp = d["bem"], d["bsp"], d.get("esp")
        grav = [0.0, 0.0, 0.0]
        for magic, b in res[i]:
            if magic == "FLD1":
                f = fld1(b)
                if f["type"] == 0:  # gravity only (ponytail: air, vortex, ... ignored)
                    grav = [g + f["mag"] * c for g, c in zip(grav, f["dir"])]
        t, vprm = 0xFFFF, True
        tdb = d.get("tdb", [])
        if bsp["tex_idx"] < len(tdb):
            from PIL import Image
            ti = tdb[bsp["tex_idx"]]
            baked, vprm = bake_texture(tile_texture(decode_bti(texs[ti][1]), texs[ti][1], bsp), bsp)
            sec = None
            for magic, b in res[i]:  # ETX1 second texture: TEV stage 1 multiplies colour and alpha by it
                if magic == "ETX1" and struct.unpack_from(">I", b, 8)[0] & 0x100 and b[0x26] < len(tdb):
                    sec = tdb[b[0x26]]
            side = min(32, baked.shape[1]), min(32, baked.shape[0])  # particles are small on screen: 32 texels
            baked = np.asarray(Image.fromarray(baked, "RGBA").resize(side, Image.Resampling.BOX)).copy()
            if sec is not None:
                s2 = np.asarray(Image.fromarray(tile_texture(decode_bti(texs[sec][1]), texs[sec][1], bsp), "RGBA")
                                .resize(side, Image.Resampling.BOX))
                baked = (baked.astype(np.uint16) * s2 // 255).astype(np.uint8)
            key = (ti, sec, colour_mode(bsp), bsp["prm"], bsp["env"]) if not vprm else (ti, sec, colour_mode(bsp)[1])
            if key not in tex_key:
                tex_key[key] = len(out_tex)
                out_tex.append(tex.to_ps2(baked, 0, 0))  # clamped: bilinear never wraps to the other edge
            t = tex_key[key]
        keys = bsp["prm_anm"][:8] if vprm else []
        blend = 1 if (bsp["blend"] >> 6 & 15) == 1 else 0  # destination factor ONE: additive
        e = esp or dict(scale_in_t=0, scale_out_t=1, scale_in_x=1, scale_out_x=1, scale_in_y=1, scale_out_y=1,
                        scale_rndm=0, alpha_in_t=0, alpha_out_t=1, alpha_in=1, alpha_base=1, alpha_out=1, flags=0)
        ef = (e["flags"] & 1) | (e["flags"] >> 16 & 1) << 1 | (e["flags"] >> 1 & 1) << 2
        d2 = [c / 32767 for c in bem["dir"]]
        recs.append(struct.pack("<6f8f12f2f7f5f4hHBBBBBB4B8h32B",
                                bem["rate"], bem["rate_rndm"], bem["life_rndm"], bem["vol_size"], bem["min_rad"],
                                bem["sweep"], bem["vel_omni"], bem["vel_axis"], bem["vel_dir"], bem["vel_rndm"],
                                bem["spread"], bem["vel_ratio"], bem["air"], bem["moment"], *bem["trs"], *d2,
                                *bem["scl"], *grav, *bsp["size"], e["scale_in_t"], e["scale_out_t"], e["scale_in_x"],
                                e["scale_out_x"], e["scale_in_y"], e["scale_out_y"], e["scale_rndm"],
                                e["alpha_in_t"], e["alpha_out_t"], e["alpha_in"], e["alpha_base"], e["alpha_out"],
                                bem["life"], bem["max_frame"], bem["start_frame"], bsp["clr_max"], t, bem["vol"], blend,
                                bem["rate_step"], ef, 1 if bem["flags"] & 8 else 0, len(keys),
                                *(bsp["prm"] if vprm else (255, 255, 255, bsp["prm"][3])),
                                *[k[0] for k in keys] + [0] * (8 - len(keys)),
                                *[c for k in keys for c in k[1]] + [0] * 4 * (8 - len(keys))))
    # WaterPlant.cpp's loose textures (WaterPlantDrawInit: WaterPlantA-D.bti, one per type), after the effects' ones:
    # header word 2 = the first one's index (-1 none), halved to 32 x 128
    from PIL import Image
    first = -1
    wp = fs if isinstance(fs, Path) else Path(fs)
    arc = wp / "ObjectData" / "WaterPlant.arc"
    if arc.exists():
        files = {k.lower(): v for k, v in B.rarc_files(arc.read_bytes())}
        first = len(out_tex)
        for c in "abcd":
            img = decode_bti(files[f"waterplant{c}.bti"])
            img = np.asarray(Image.fromarray(img, "RGBA").resize((img.shape[1] // 2, img.shape[0] // 2), Image.Resampling.BOX)).copy()
            out_tex.append(tex.to_ps2(img, 0, 0))
        # PlantStalkDrawInit: Plant.arc's PlantStalk.bti right after them (fx_plantstalk_tex)
        img = decode_bti({k.lower(): v for k, v in B.rarc_files((wp / "ObjectData" / "Plant.arc").read_bytes())}["plantstalk.bti"])
        out_tex.append(tex.to_ps2(np.ascontiguousarray(img), 0, 0))
        # DinoPackun's FootPrint texture (getTexFromArc DinoPackunFootprint.bti) after it (fx_dinofoot_tex)
        img = decode_bti({k.lower(): v for k, v in B.rarc_files((wp / "ObjectData" / "DinoPackun.arc").read_bytes())}["dinopackunfootprint.bti"])
        out_tex.append(tex.to_ps2(np.ascontiguousarray(img), 0, 0))
        # OceanSphere (WaterWave.arc) after it (fx_ocean_tex 0, 1): its face TEV baked with one sample of OceanSphere.bti
        # for both scrolled stages, lerp(C1 00 51 70, C2 white, clamp(2 x K0 78 FF FF x T x T)), alpha A1 6F; its back
        # lerp(C0 00 69 B8, C1 black, OceanSphereEnvRef.bti), the env map mirrored twice over (its wrap 2)
        ww = {k.lower(): v for k, v in B.rarc_files((wp / "ObjectData" / "WaterWave.arc").read_bytes())}
        t = decode_bti(ww["oceansphere.bti"]).astype(np.float32) / 255
        t = np.asarray(Image.fromarray((t * 255).astype(np.uint8), "RGBA").resize((64, 64), Image.Resampling.BOX)).astype(np.float32) / 255
        k0 = np.array([0x78, 0xFF, 0xFF], np.float32) / 255
        c1 = np.array([0x00, 0x51, 0x70], np.float32) / 255
        v = np.clip(2 * k0 * t[..., :3] * t[..., :3], 0, 1)
        face = np.zeros((64, 64, 4), np.uint8)
        face[..., :3] = ((c1 + (1 - c1) * v) * 255 + 0.5).astype(np.uint8)
        face[..., 3] = 0x6F
        out_tex.append(tex.to_ps2(face, 1, 1))
        e = decode_bti(ww["oceansphereenvref.bti"]).astype(np.float32) / 255
        e = np.concatenate((e[:, ::-1], e), 1)
        e = np.concatenate((e[::-1], e), 0)
        e = np.asarray(Image.fromarray((e * 255).astype(np.uint8), "RGBA").resize((64, 64), Image.Resampling.BOX)).astype(np.float32) / 255
        c0 = np.array([0x00, 0x69, 0xB8], np.float32) / 255
        back = np.zeros((64, 64, 4), np.uint8)
        back[..., :3] = ((c0 * (1 - e[..., :3])) * 255 + 0.5).astype(np.uint8)
        back[..., 3] = 255
        out_tex.append(tex.to_ps2(back, 0, 0))
    head = struct.pack("<IIiI", len(used), len(defs), first, len(autos))
    return head + b"".join(defs) + b"".join(recs) + b"".join(autos), out_tex


# classes whose emitEffect calls game.c makes by name (fx_named), besides the skinned ones (their joints: -1)
NAMED = ("KillerGunnerSingle", "ItemBubble", "DinoPackunBall")


def anim_effects(fs, auto, add_def):
    """EffectKeeper's AutoEffectList rows of the skinned placed classes (slice.py SKINNED_OBJ) and NAMED: each
    {char class[24], char clip[24], u16 start, u16 end, u16 effect (FxDef index, added after FX's), s16 joint (the
    model's JNT1 index, -1 its base), f32 offset[3] (joint space), f32 scale}. Animation-driven rows (AnimName set):
    clip its lower-case clip ("<class>/<clip>"), start its StartFrame, end 0xFFFF (one shot at start) or its EndFrame
    (kept from start to end). The others (emitEffect by name: fx_named): clip their UniqueName in lower case, start
    0xFFFF. ponytail: no PrmColor / EnvColor, no Follow (a one-shot stays where it was emitted)."""
    import slice as S
    import skin
    import tex
    col = lambda c: B.bcsv_column(auto, c)
    g, u, e, an, jn = col("GroupName"), col("UniqueName"), col("EffectName"), col("AnimName"), col("JointName")
    num = lambda c: S.float_column(auto, c) or [0.0] * len(g)
    ox, oy, oz, sv = num("OffsetX"), num("OffsetY"), num("OffsetZ"), num("ScaleValue")
    st, en = B.bcsv_column(auto, "StartFrame"), B.bcsv_column(auto, "EndFrame")
    out, def_of, joints_of = [], {}, {}
    for c in tuple(S.SKINNED_OBJ) + NAMED:
        arcname = S.ANIM_ARC.get(c, c)
        if c not in joints_of:
            arc = Path(fs) / "ObjectData" / f"{arcname}.arc"
            names_ = []
            if arc.exists():
                mb = next((v for k, v in B.rarc_files(arc.read_bytes()) if k.lower().endswith((".bdl", ".bmd"))), None)
                if mb:
                    names_ = [j["name"] for j in skin.joints(mb, tex.blocks(mb))]
            joints_of[c] = names_
        for i in range(len(g)):
            if g[i] not in (c, arcname) or not e[i]:
                continue
            if e[i] not in def_of:
                def_of[e[i]] = add_def(e[i])
            j = joints_of[c].index(jn[i]) if jn[i] in joints_of[c] else -1
            if not an[i]:  # emitEffect(<UniqueName>)
                out.append(struct.pack("<24s24sHHHh4f", c.lower().encode()[:23], u[i].lower().encode()[:23], 0xFFFF,
                                       0xFFFF, def_of[e[i]], j, ox[i], oy[i], oz[i], sv[i] or 1.0))
                continue
            end = 0xFFFF if en[i] in (None, -1, 0xFFFFFFFF) or en[i] >= 0xFFFF else en[i]
            for clip in an[i].lower().split():  # "Eat2 Excite2 Joy2": each of them
                out.append(struct.pack("<24s24sHHHh4f", c.lower().encode()[:23], clip.encode()[:23],
                                       min(int(st[i] or 0), 0xFFFE), end, def_of[e[i]], j, ox[i], oy[i], oz[i], sv[i] or 1.0))
    return out


def header(path):
    """engine/src/fx_tab.h: FX_* ids in FX order."""
    ids = [f"FX_{(g + '_' + u).upper()}" for g, u in FX if u is not None]
    amb = ", ".join(f'"{n}"' for n in AMBIENT)
    Path(path).write_text("/* written by tools/jpa.py --header: effect ids (tools/jpa.py FX) */\n"
                          "enum { " + ", ".join(ids) + ", FX_AMBIENT_FIRST };\n"
                          f"#define FX_AMBIENT_N {len(AMBIENT)}\n"
                          "#define FX_COUNT (FX_AMBIENT_FIRST + FX_AMBIENT_N)\n"
                          f"static const char *const fx_ambient[FX_AMBIENT_N] = {{{amb}}};\n", newline="\n")


def selftest():
    bem = bytearray(0x7C)
    bem[0:4] = b"BEM1"
    struct.pack_into(">II", bem, 4, 0x7C, 0x0100)
    struct.pack_into(">f", bem, 0x4C, 2.5)
    struct.pack_into(">hhhHH", bem, 0x6E, 30, 0, 20, 50, 0)
    j = b"JPAC2-10" + struct.pack(">HHI", 1, 0, 0x10 + 8 + len(bem)) + struct.pack(">HHBBBx", 7, 1, 0, 0, 0) + bem
    res, texs = read_jpc(j)
    d = describe(res[7])["bem"]
    assert d["rate"] == 2.5 and d["vol"] == 1 and d["life"] == 20 and d["max_frame"] == 30 and texs == []
    import numpy as np
    t = np.zeros((1, 2, 4), np.uint8)
    t[0, 1] = 255  # texel 0 black, texel 1 white
    bsp = dict(flags=3 << 15, prm=(255, 0, 0, 255), env=(0, 0, 255, 255))  # mode 3: lerp(env, prm, T)
    out, vprm = bake_texture(t, bsp)
    assert out[0, 0, :3].tolist() == [0, 0, 255] and out[0, 1, :3].tolist() == [255, 0, 0] and not vprm
    assert bake_texture(t, dict(flags=1 << 15, prm=(1, 2, 3, 4), env=(0, 0, 0, 0)))[1]  # mode 1: prm in vertices
    bti = bytes(6) + bytes([2, 0])  # S mirror, T clamp
    m = tile_texture(t, bti, dict(flags=1 << 25 | 1 << 26))
    assert m.shape == (2, 4, 4) and m[0, :, 0].tolist() == [0, 255, 255, 0] and m[1].tolist() == m[0].tolist()
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif sys.argv[1] == "--header":
        header(sys.argv[2])
    elif sys.argv[1] == "--pack":
        payload, tx = pack(sys.argv[2])
        print(len(payload), "B,", struct.unpack_from("<II", payload)[0], "resources,", len(tx), "textures",
              [(t["w"], t["h"]) for t in tx])
    else:
        res, texs, names, _ = load(sys.argv[1])
        for e in sys.argv[2:]:
            for i in effect_ids(names, e):
                d = describe(res[i])
                print(e, i, {k: v for k, v in d.items()}, [texs[t][0] for t in d.get("tdb", [])])
