"""Phase-1 slice converter: one SMG scenario -> one little-endian blob for the PS2 engine.

usage: python tools/slice.py data/fs/files HeavensDoorGalaxy 2 out/slice.bin [--simplify=R]
       python tools/slice.py --selftest

Which stages and layers: budget.py's scenario logic (ScenarioData masks, StageObjInfo placement, Petari
StageDataHolder). Rows read from `jmp/placement/<layer>/{objinfo,planetobjinfo}` and `jmp/start/<layer>/startinfo`;
zones placed by `stageobjinfo` rows of the stage that holds them (pos + dir in degrees, StageDataHolder
calcPlacementMtx). Rails from `jmp/path/commonpathinfo` + `commonpathpointinfo.<n>` (control point pnt0 only).
Models: every placed planet (PlanetMapDataTable names) and the archives of the object types in OBJ_TYPES, through
geom.py (triangles per matrix group, simplified to R when R < 1). KCL copied as on disc (big-endian) from each planet
archive, converted to little-endian (kcl_to_le). Streaming layout of a scenario: packs() (task 5.1).

Blob (all little-endian; every payload starts 16-byte aligned, for DMA):
  header  'SMGS' u32 version=5, u32 chunk count, u32 pad          (16 B)
  chunk   tag[4], u32 payload size, 8 B pad, payload               (16 B header, payload padded to 16)
  'TYPE'  u32 n, n × char[32]                  type names; OBJS/STRT rows index this table
  'ZONE'  u32 n, n × {char[32] name, f32 pos[3], f32 rot[3]}   world placement; zone 0 = main stage (identity).
          Everything else placed in a zone is already in world space (to_world); CAMS stays zone-local
  'ZPAR'  u32 n, n × s32 parent zone (-1 for the main stage): the zone chain for streaming
  'OBJS'  u32 n, n × ROW;  'STRT' same layout (Mario start points, arg0 their MarioNo, arg1 their Camera_id)
          ROW = {u16 type, u16 zone, s32 l_id, s32 path_id, f32 pos[3], f32 rot[3], f32 scale[3], s32 arg[8],
                 s16 sw[5] (SW_FIELDS, -1 none), s16 group (GroupId, -1 none)}  (92 B)
  'PATH'  u32 n, n × {u16 zone, u16 closed, s32 id, u32 npts, u32 nraw, s32 stop_type (path_arg1), s32 pad,
          f32 pts[npts][3], f32 raw[nraw][3], nraw × {f32 coord, s32 arg0, s32 arg5}, nraw × {s32 arg1, s32 arg2}}:
          pts the rail as a polyline (Bezier parts sampled, bezier_polyline), raw its JMap points (pnt0), each with its
          distance along pts and its point_arg0 / point_arg5 (map parts: speed, stop time), point_arg1 / 2 (Rock)
  'MODL'  u32 n, n × {char[32] name, f32 bounding radius}
  'KCL '  u32 n, n × {char[32] model, u32 size, bytes[size] (little-endian KCL, kcl_to_le), pad to 4}
  'VUMD'  u32 n, 12 pad, n × {char[32] name, u32 ngroups, f32 qscale, 8 pad, ngroups × {u32 nverts (3 per
          triangle), u16 tex, u16 nbatch, u8 blend (0 opaque, 1 blended, 2 additive), u8 z_write, u8 alpha, u8 lit,
          4 pad, nbatch × {u16 emitted, u16 unique} padded to 16, then per batch (index_batches): s16 pos[unique][4]
          (x, y, z in steps of qscale, w = 7 * matrix slot), s16 uv[unique][2] if textured, s8 normal[unique][4] if
          lit (x 127), u8 rgbi[emitted][4] (colour + unique index), each array padded to 16}}
  'LGHT'  the scenario's area lights and its zones' LightID tables (tools/light.py)
  'TLGT'  u8 per TYPE: its actor light type (tools/lighttype.py, light_types.json: 0 Player 1 Strong 2 Weak 3 Planet, 255)
  'GRAV'  u32 n, n × {u16 kind, u16 flags, s32 priority, s32 inverse, f32 range, f32 distant, f32 m[12] (x, y, z
          axes and translation, world), f32 p[4], s32 OBJS row}  (88 B): grav_record, Petari GravityCreator.cpp per kind; then
          the wires' points, f32 xyz each
"""
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B
import camera
import geom
import jpa
import light
import msg
import numpy as np
import skin
import tex

RESIDENT = ("Coin", "StarPiece", "SuperSpinDriver")  # OBJ_TYPES in phase 0b's RESIDENT set (out/sets.json): the resident pack
SKINNED = ("Mario",)
# placed classes drawn skinned and animated by their own BCKs (the scenario's ANIM chunk, "<class>/<clip>" in
# lower case); their model archive is the class name
SKINNED_OBJ = ("TreasureBoxKinokoOneUp", "TreasureBoxCoin", "TreasureBoxEmpty", "TreasureBoxGoldEmpty", "TreasureBoxCrackedKinokoOneUp", "TreasureBoxCrackedAirBubble", "TreasureBoxBlueChip", "TreasureBoxYellowChip", "TreasureBoxKinokoLifeUp", "TreasureBoxCrackedKinokoLifeUp", "TreasureBoxCrackedEmpty", "DesertLandDownSwitch", "ValveSwitch", "SpinLeverSwitch", "LavaBreakColumn", "LavaObstacleRockHomeB", "LavaFallDownColumnA", "LavaFallDownColumnB", "HeavenlyBeachUnderRock", "ShutterDoorC", "ShutterDoorD", "ShutterDoorE", "MechaKoopaPartsCaterpillar", "IceVolcanoAppearStepA", "IceVolcanoAppearStepB", "IceVolcanoClimbingWall", "IceUpDownShutterDoorA", "LavaBreakRockA", "HeavenlyBeachTopRock", "LavaBreakBridge", "LavaBreakRockB", "LavaBreakRockC", "LavaBreakRockD", "TeresaRacePartsB", "TeresaRoomDoor", "JumpBeamer", "JumpBeamerHead", "BombHei", "BigFan", "BigFanNoLeg", "BigFanWind", "DesertMovingLand", "LavaHomeVolcanoFlow", "DarknessRoomDoorA", "TeresaMansionDoorA", "TeresaMansionDoorB", "TeresaMansionEntranceDoor", "Pukupuku", "Kabokuri", "Snakehead", "SnakeheadSmall", "SunakazeKun", "TwisterSan", "TwisterSanSlim", "SunakazeKunTrap", "SeaGull", "FishA", "FishB", "FishC", "FishD", "FishE", "FishF", "TeresaWater", "CoconutTreeLeaf", "EyeBeamer", "EyeBeamerBeam", "KoopaBattleMapStair", "KoopaBattleMapStairBig", "KoopaBattleMapStairTurn", "WaterLeakPipe", "FirePressureRadiate", "WaterPressure", "WaterBullet", "Metbo", "Mogucchi", "MogucchiHole", "Mogu", "MoguStone", "MoguHole", "Kanina", "KaninaRed", "Jellyfish", "BasaBasa", "BasaBasaIce", "Gesso", "SearchBeamer", "RingBeamer", "JumpBeamerBeam", "FlowerGroup", "FlowerBlueGroup", "CutBushGroup", "DinoPackun", "OtaKing", "OtaKingFoot", "TicoFat", "Tico", "CocoSambo", "CocoSamboHead", "Kuribo", "Takobo",
               "Karikari", "Butterfly", "PackunPetit", "KuriboMini", "DinoPackunEggShellBreak", "PackunPetitHead",
               "RockBreak", "WanwanRollingBreak", "PunchingKinoko", "OtaKingMagma", "OtaKingLongFoot", "WoodBoxBreak",
               "CrystalCageSBreak", "BenefitItemOneUp", "BenefitItemLifeUp", "FlipPanel", "FlipPanelReverse", "OtaRock",
               "HammerHeadPackun", "PackunLeaf", "Plant", "Petari", "Butler", "TicoDomeLecture",
               "PhantomShipPropellerSmall", "PhantomShipPropellerBig", "PhantomShipBridge")  # AnmModelSwitchMove without SW_A: Move at once (obj_anim: its only clip)
# MapObjActor::initialize (tryStartAllAnim): a placed class whose model archive holds a BCK of the archive's name
# plays it from the start; such classes are drawn skinned too (AUTO_SKINNED, class -> archive, filled by placement)
AUTO_SKINNED = {}
# the demo camera sheets' anim cameras (DemoCameraKeeper AnimCameraName: a .canm of the target cast's archive): (class,
# canm stem) per scenario, filled by demo_chunk and baked by canm_chunk (called after it)
DEMO_CANM = set()
_auto_anim = {}


def auto_anim(fsp, arc):
    """The archive holds <arc>.bck (tryStartAllAnim's own-named clip)."""
    if arc not in _auto_anim:
        p = Path(fsp) / "ObjectData" / f"{arc}.arc"
        _auto_anim[arc] = p.exists() and any(n.rsplit("/", 1)[-1].lower() == arc.lower() + ".bck"
                                             for n, _ in B.rarc_files(p.read_bytes()))
    return _auto_anim[arc]


ANIM_ARC = {"TicoDomeLecture": "Tico", "CocoSambo": "CocoSamboBody", "Karikari": "Karipon", "BenefitItemOneUp": "KinokoOneUp",
            "BenefitItemLifeUp": "KinokoLifeUp", "HammerHeadPackun": "PackunFlower", "Plant": "PlantSeed", "FlowerGroup": "Flower", "FlowerBlueGroup": "FlowerBlue",
            "CutBushGroup": "CutBush", "FirePressureRadiate": "FirePressure", "JumpBeamer": "JumpBeamerBody", "TreasureBoxKinokoOneUp": "TreasureBox", "TreasureBoxCoin": "TreasureBox", "TreasureBoxEmpty": "TreasureBox", "TreasureBoxGoldEmpty": "TreasureBoxGold", "TreasureBoxCrackedKinokoOneUp": "TreasureBoxCracked", "TreasureBoxCrackedAirBubble": "TreasureBoxCracked", "TreasureBoxBlueChip": "TreasureBox", "TreasureBoxYellowChip": "TreasureBox", "TreasureBoxKinokoLifeUp": "TreasureBox", "TreasureBoxCrackedKinokoLifeUp": "TreasureBoxCracked", "TreasureBoxCrackedEmpty": "TreasureBoxCracked"}  # where a class's clips are when not in its own-named archive
# ponytail: models the renderer cannot reproduce: the atmosphere spheres (*Air: their glow is a TEV rim effect; drawn
# with their material they flood the screen)
NO_DRAW = ("SphereAir", "DimensionAir", "HomeAir", "TwilightAir", "DarknessRoomAir", "SunsetAir", "FineAir")   # skinned models converted with SKIN data (milestone 3); Mario's clips go in ANIM
OBJ_TYPES = ("Coin", "PurpleCoin", "StarPiece", "Kuribo", "CirclePurpleCoinGroup", "PurpleRailCoin", "CircleCoinGroup",
             "RailCoin", "SuperSpinDriver", "StarPieceGroup", "StarPieceFlow")
# classes whose first NameObjFactory archive is not their model (OtaKing's is FireBall, Tico's TicoBaby): the model
MODEL_ARC = {"TicoDomeLecture": "Tico", "OtaKing": "OtaKing", "KoopaJr": "KoopaJr", "Tico": "Tico", "Plant": "PlantSeed"}
# ponytail: no MiniatureGalaxyStarNumber (its digits are texture matrices per material), no MiniatureGalaxyShadow
MINI_EXTRA = ("MiniatureGalaxyUnknown", "MiniHatenaGalaxy", "MiniatureGalaxyUnknownKoopa", "MiniKoopaGalaxy",
              "MiniatureGalaxySelect")
SW_FIELDS = ("SW_APPEAR", "SW_A", "SW_B", "SW_DEAD", "SW_SLEEP")  # stage switch ids: < 1000 the zone's, else global
ROW_FIELDS = ("pos_x", "pos_y", "pos_z", "dir_x", "dir_y", "dir_z", "scale_x", "scale_y", "scale_z")
ROW = struct.Struct("<HHii9f8i5hh")
# sky archives whose star textures are projected from positions (spherical UVs instead, model_groups)
SKY_ARCS = ("VROrbit", "VRDarkSpace", "VRSandwichSun", "GalaxySky", "MilkyWaySky", "HalfGalaxySky", "PhantomSky",
            "KoopaVS1Sky", "KoopaVS2Sky", "StarrySky", "RockPlanetOrbitSky", "GreenPlanetOrbitSky")
SKY_TILES = 8
# VROrbit's sea materials (their MAT3: EarthNightMat_v K0 + lerp(C0 0, TEX, RAS) + RAS x TEX; EarthFar_v RAS x TEX +
# lerp(C1, K1, EarthFarK) + RAS x TEX): (flat base or None, base texture TEX1 index, its matrix slot, C1, K1)
SKY_SEA = {"EarthNightMat_v": ((0, 25, 52), None), "EarthFar_v": (None, 7, 1, (0, 25, 52), (0, 151, 230))}

# second models a class draws besides its own (FireBar.cpp: FireBarBall around the FireBarCore)
# extra models a class draws: a name of its own, or "#<BCK>": the class's archive with that animation's visibility
# ("<archive>#<BVA>": another archive with that BVA's last-frame visibility: the cracked egg shell)
# (FlipPanel: "?" at rest, "!" once flipped, the End face when its group is complete)
EXTRA = {"StarReturnDemoStarter": ("PowerStar", "GrandStar"), "FireBar": ("FireBarBall",), "FlipPanel": ("#PanelA", "#PanelEnd"), "FlipPanelReverse": ("#PanelA", "#PanelEnd"),
         "IronCannonLauncher": ("KoopaJrShipCannonShell",),
         "MorphItemNeoFire": ("MarioFireBall",), "AirBubbleGenerator": ("AirBubble",),
         "DinoPackun": ("DinoPackunTailBall", "DinoPackunEggShell", "DinoPackunEggShell#EggBroken", "DinoPackunEggShellBreak"), "OtaKing": ("CocoNut", "OtaKingFoot", "FireBall", "FireBubble", "OtaKingMagma", "OtaKingLongFoot", "OtaKingMagmaBloom"), "OtaRock": ("CocoNut", "FireBall"), "HammerHeadPackun": ("PackunLeaf",), "Plant": ("PlantTop", "PlantLeaf"), "CocoSambo": ("CocoSamboHead",), "PackunPetit": ("PackunPetitHead",),
         "RockCreator": ("RockBreak",), "WanwanRolling": ("WanwanRollingBreak",),
         "WoodBox": ("#WoodBox", "WoodBoxBreak"), "CrystalCageS": ("#CrystalCageS", "CrystalCageSBreak"),
         "CrystalCageM": ("#CrystalCageM", "CrystalCageSBreak"), "RingBeamer": ("JumpBeamerBeam",), "Mogu": ("MoguStone", "MoguHole"), "Mogucchi": ("MogucchiHole", "MogucchiHillA", "MogucchiHillB", "MogucchiHillC"), "WaterPressure": ("WaterBullet",), "EyeBeamer": ("EyeBeamerBeam",), "FishGroupA": ("FishA",), "FishGroupB": ("FishB",), "FishGroupC": ("FishC",), "FishGroupD": ("FishD",), "FishGroupE": ("FishE",), "FishGroupF": ("FishF",), "SeaGullGroup": ("SeaGull",), "SunakazeKun": ("SunakazeKunTrap",), "ChooChooTrain": ("ChooChooTrainBody",), "BigFan": ("BigFanWind",), "BigFanNoLeg": ("BigFanWind",), "BombHeiLauncher": ("BombHei",), "BombLauncher": ("BombHei",), "JumpBeamer": ("JumpBeamerBeam", "JumpBeamerHead")}
# model-less classes the engine runs (stage switch logic): kept in OBJS like the classes with a model
# power stars an event brings out (Petari PowerStarEventKeeper): ScenarioData AppearPowerStarObj names each star's
# declarer by its ObjNameTable name. The classes the engine runs, with the arg giving an explicit star id (None: the
# one their name finds); BreakableCage* only with a power star inside (arg7 7). The star's arg7 gets the declarer's
# OBJS index (-2: a hundred coins), the engine keeps it hidden until then (star_wait)
STAR_DECLARERS = {"FlipPanelObserver": 7, "CrystalCageL": 0, "BreakableCage": None, "BreakableCageRotate": None,
                  "BreakableCageL": None}
COIN100 = "１００枚コイン"
KCL_JOINT = {"JumpStand": "JumpStandJoint03"}  # collision at that joint's (bind pose) place
# drawn and collided at the last frame of a BCK, the named KCLs added at their joint (PhantomShipBridge.cpp:
# setStateMoveA, the bridge down without SW_A; createCollisionPartsFromLiveActor "Move" on joint Move).
# ponytail: the end pose for good (no MoveA / MoveB playing on the switches); PhantomShipBridge itself moves (KCL_PARTS)
# AstroMapObjFunction::getModelName: these classes take their model from a table by arg0 (the dome, 1-6)
ASTRO_MODELS = {
    "AstroDome": ("AstroDomeObservatory", "AstroDomeWell", "AstroDomeKitchen", "AstroDomeBedRoom", "AstroDomeMachine",
                  "AstroDomeTower"),
    "AstroDomeSky": ("AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC", "AstroDomeSkyA", "AstroDomeSkyB", "AstroDomeSkyC"),
    "AstroDomeEntrance": ("AstroDomeEntranceObservatory", "AstroDomeEntranceWell", "AstroDomeEntranceKitchen",
                          "AstroDomeEntranceBedRoom", "AstroDomeEntranceMachine", "AstroDomeEntranceTower"),
    "AstroStarPlate": ("AstroStarPlateObservatory", "AstroStarPlateWell", "AstroStarPlateKitchen", "AstroStarPlateBedRoom",
                       "AstroStarPlateMachine", "AstroStarPlateTower")}
POSE = {"NutShipFleetBridge": ("MoveA", {"move.kcl": "Move"})}
# collisions on a joint of a skinned model (createCollisionPartsFromLiveActor with getJointMtx): KCL "<class>#<joint>"
# in that joint's space, the engine moves it with the joint (PhantomShipBridge.cpp: "Move" on joint Move)
KCL_PARTS = {"PhantomShipBridge": {"move.kcl": "Move"}, "DarknessRoomDoorA": {"open.kcl": "Open"},
             "TeresaMansionDoorA": {"open.kcl": "Open"}, "TeresaMansionDoorB": {"open.kcl": "Open"},
             "TeresaMansionEntranceDoor": {"open.kcl": "Open"},
             "DesertLandDownSwitch": {"move.kcl": "Move"}}  # TypicalDoor's Open collision too
HOME_POS = "\u5408\u4f53\u30d6\u30ed\u30c3\u30af\u6545\u90f7\u70b9"  # 合体ブロック故郷点: AssemblyBlock's home (Petari sReturnPosName)
LOGIC = ("TimerSwitch", "SwitchSynchronizerReverse", "GroupSwitchWatcher", "CollisionBlocker", "FlipPanelObserver",
         "YellowChipGroup", "BlueChipGroup", "SwitchCube", "SwitchSphere", "SwitchCylinder", "DeathCube", "DeathSphere",
         "DeathCylinder", "RestartCube", "StarPieceSpot", "WaterCube", "WaterSphere", "WaterCylinder",
         "AstroChangeStageCube", "MovieStarter")  # areas (areaobjinfo) travel as OBJS rows too


def col(b, field, n, default=0):
    v = B.bcsv_column(b, field)
    return v if len(v) == n else [default] * n


def float_column(b, field):
    """BCSV float (type 2) column: bcsv_column only decodes int/string types, so read floats here (same header
    layout, JMapInfo.hpp:6-31)."""
    if len(b) < 16:
        return []
    n, nf, data, esize = struct.unpack_from(">4I", b, 0)
    want = B.jmap_hash(field)
    for i in range(nf):
        fh, mask, foff, shift, ftype = struct.unpack_from(">IIHBB", b, 16 + i * 12)
        if fh == want:
            if ftype != 2:
                return []
            return [struct.unpack_from(">f", b, data + e * esize + foff)[0] for e in range(n)]
    return []


def int_or(b, field, n, default):
    v = B.bcsv_column(b, field)
    v = [x - 2**32 if isinstance(x, int) and x >= 2**31 else x for x in v]
    return v if len(v) == n else [default] * n


def rows(b):
    names = B.bcsv_column(b, "name")
    n = len(names)
    if not n:
        return []
    fl = {f: (float_column(b, f) or [0.0] * n) for f in ROW_FIELDS}
    args = [col(b, f"Obj_arg{i}", n, -1) for i in range(8)]
    lid = col(b, "l_id", n, -1)
    path = [v - 0x10000 if 0x8000 <= v < 0x10000 else v for v in col(b, "CommonPath_ID", n, -1)]  # SHORT, unsigned in bcsv_column
    sw = {f: int_or(b, f, n, -1) for f in SW_FIELDS}
    group = int_or(b, "GroupId", n, -1)
    camset = int_or(b, "CameraSetId", n, -1)
    mid = int_or(b, "MessageId", n, -1)
    demo, cast = int_or(b, "DemoGroupId", n, -1), int_or(b, "CastId", n, -1)  # DemoDirector::registerDemoCast
    out = []
    for i in range(n):
        out.append(dict(name=names[i], l_id=lid[i], path=path[i], camset=camset[i], pos=[fl[f][i] for f in ROW_FIELDS[:3]],
                        rot=[fl[f][i] for f in ROW_FIELDS[3:6]], scale=[fl[f][i] or 1.0 for f in ROW_FIELDS[6:]],
                        args=[a[i] for a in args], range=(float_column(b, "Range") or [-1.0] * n)[i],
                        distant=(float_column(b, "Distant") or [None] * n)[i],
                        priority=int_or(b, "Priority", n, 0)[i], inverse=int_or(b, "Inverse", n, 0)[i],
                        gtype=(B.bcsv_column(b, "Gravity_type") or ["Normal"] * n)[i],
                        sw=[sw[f][i] for f in SW_FIELDS], group=group[i], mid=mid[i], demo=demo[i], cast=cast[i],
                        cls=names[i]))
    return out


def map_parts(b, rs):
    """mappartsinfo rows: Obj_arg0-3 as usual, the MapPartsRotator columns in args 4-7: RotateSpeed, RotateAngle,
    RotateAxis, then bit 30 (a map part) | RotateType (bits 0-3, default 1) | RotateAccelType << 4 | MoveConditionType
    << 8 | RotateStopTime << 12 (unset: Petari's defaults, 0 but the type)."""
    n = len(rs)
    col = {f: int_or(b, f, n, -1) for f in ("RotateSpeed", "RotateAngle", "RotateAxis", "RotateType", "RotateAccelType",
                                           "MoveConditionType", "RotateStopTime")}
    for i, r in enumerate(rs):
        g = lambda f, d: col[f][i] if col[f][i] >= 0 else d
        r["args"][4:8] = [col["RotateSpeed"][i], col["RotateAngle"][i], col["RotateAxis"][i],
                          1 << 30 | g("RotateType", 1) & 15 | (g("RotateAccelType", 0) & 15) << 4 |
                          (g("MoveConditionType", 0) & 15) << 8 | min(g("RotateStopTime", 0), 0x3FFF) << 12]
    return rs


def stage_files(path):
    return {p.lower(): d for p, d in B.rarc_files(path.read_bytes(), paths=True)}


def scene(fs, galaxy, scenario):
    fs = Path(fs)
    st = fs / "StageData"
    cache = {}
    sd = {n.lower(): d for n, d in B.rarc_files((st / galaxy / f"{galaxy}Scenario.arc").read_bytes())}["scenariodata.bcsv"]
    up = {}
    act = B.active_stages(galaxy, B.scenario_masks(sd)[scenario],
                          lambda s: B.arc_layers(st / f"{s}.arc", cache)["layers"] if (st / f"{s}.arc").exists() else {}, up)
    zones, objs, starts, paths = [dict(name=galaxy, pos=[0, 0, 0], rot=[0, 0, 0])], [], [], []
    cubes, bcams = [], []  # camera areas (cameracubeinfo rows) and each stage's cameraparam.bcam
    zone_id = {galaxy: 0}
    homes = {}  # (zone, Obj_ID) -> the GeneralPos HOME_POS linked to that objinfo row
    demos, gpos = [], []  # DemoGroup / DemoSubGroup rows (demoobjinfo); every GeneralPos (MR::findNamePos)
    order = [galaxy] + [s for s in act if s != galaxy]
    for s in order:
        files = stage_files(st / f"{s}.arc")
        root = next(iter(files)).split("/")[0]
        for layer in act[s]:
            for z in rows(files.get(f"{root}/jmp/placement/{layer}/stageobjinfo", b"")):
                if z["name"] in act and z["name"] not in zone_id:
                    zone_id[z["name"]] = len(zones)
                    zones.append(dict(name=z["name"], pos=z["pos"], rot=z["rot"]))
        zid = zone_id.get(s, 0)
        for layer in act[s]:
            for f in ("objinfo", "planetobjinfo", "areaobjinfo"):
                for r in rows(files.get(f"{root}/jmp/placement/{layer}/{f}", b"")):
                    objs.append(dict(r, zone=zid))
            for r in rows(files.get(f"{root}/jmp/childobj/{layer}/childobjinfo", b"")):
                objs.append(dict(r, zone=zid))
            mp = files.get(f"{root}/jmp/mapparts/{layer}/mappartsinfo", b"")
            for r in map_parts(mp, rows(mp)):
                objs.append(dict(r, zone=zid))
            gp = files.get(f"{root}/jmp/generalpos/{layer}/generalposinfo", b"")
            for r, pn, oid in zip(rows(gp), B.bcsv_column(gp, "PosName"), int_or(gp, "Obj_ID", len(B.bcsv_column(gp, "name")), -1)):
                if pn == HOME_POS and oid >= 0:
                    homes[(zid, oid)] = dict(zone=zid, pos=r["pos"], rot=r["rot"])
                gpos.append(dict(zone=zid, name=pn, pos=list(r["pos"]), rot=list(r["rot"])))
            db = files.get(f"{root}/jmp/placement/{layer}/demoobjinfo", b"")
            for r, dn, ts in zip(rows(db), B.bcsv_column(db, "DemoName"), B.bcsv_column(db, "TimeSheetName")):
                demos.append(dict(r, zone=zid, demo_name=dn, sheet=ts))
            sb = files.get(f"{root}/jmp/start/{layer}/startinfo", b"")
            ns = len(B.bcsv_column(sb, "name"))
            for r, no, cid in zip(rows(sb), int_or(sb, "MarioNo", ns, 0), int_or(sb, "Camera_id", ns, -1)):
                starts.append(dict(r, zone=zid, args=[no, cid] + r["args"][2:]))  # arg0: MarioNo (RestartCube arg0), arg1: Camera_id
            cb = files.get(f"{root}/jmp/placement/{layer}/cameracubeinfo", b"")
            valid, sw_a = B.bcsv_column(cb, "Validity"), int_or(cb, "SW_A", len(B.bcsv_column(cb, "name")), -1)
            for i, r in enumerate(rows(cb)):
                cubes.append(dict(r, zone=zid, invalid=i < len(valid) and valid[i] == "Invalid", sw_a=sw_a[i]))
        bcams.append((zid, files.get(f"{root}/camera/cameraparam.bcam", b"")))
        info = files.get(f"{root}/jmp/path/commonpathinfo", b"")
        ids, closed = col(info, "l_id", len(B.bcsv_column(info, "name")), -1), B.bcsv_column(info, "closed")
        for i, pid in enumerate(ids):
            pts = files.get(f"{root}/jmp/path/commonpathpointinfo.{i}", b"")
            p0, p1, p2 = ([float_column(pts, f"pnt{k}_{a}") for a in "xyz"] for k in range(3))
            npt = len(p0[0])
            paths.append(dict(zone=zid, id=pid, closed=int(str(closed[i] if i < len(closed) else "").lower() == "close"),
                              pts=list(zip(*p0)), cin=list(zip(*p1)) or list(zip(*p0)), cout=list(zip(*p2)) or list(zip(*p0)),
                              stop=int_or(info, "path_arg1", len(ids), -1)[i], arg0=int_or(pts, "point_arg0", npt, -1),
                              arg5=int_or(pts, "point_arg5", npt, -1), arg1=int_or(pts, "point_arg1", npt, -1),
                              arg2=int_or(pts, "point_arg2", npt, -1)))
    for o in objs:
        if o["name"].startswith("AssemblyBlock") and (o["zone"], o["l_id"]) in homes:
            o["home"] = homes[(o["zone"], o["l_id"])]
    return act, zones, objs, starts, paths, cubes, bcams, up, demos, gpos


# static models drawn with a BVA's visibility at a frame (the shapes it hides there are left out; None: its last):
# the egg shell holds its intact and cracked pieces, normal.bva shows the intact ones (DinoPackun::initEggShell);
# CrystalCage::init startBva(obj_name) at 0 (whole; its last frame is the broken cage); PowerStar setupColor: frame 0 the
# coloured star (1, the empty one of a star already got: ponytail, not drawn in the stages)
VIS = {"PowerStar": ("PowerStar", 0), "DinoPackunEggShell": ("normal", None), "CrystalCageS": ("CrystalCageS", 0), "CrystalCageM": ("CrystalCageM", 0)}
# models baked with a BRK's registers at a frame (Tico.cpp: ColorChange at its colour, frame 0 the yellow one)
# startBrk + setBrkFrameAndStop(argN): one model per frame its rows use, "<class>~<frame>" baked at that frame (game.c
# brk_model picks it by the row's arg). Butterfly: startBrk Butterfly, frame mColorIndex = arg0 (NoInit: 0)
BRK_ARG = {"Butterfly": ("butterfly", 0)}
# MR::initDLMakerMatColor0 per instance: one model per colour, "<class>@<k>" with material colour 0 = the k-th colour
# (game.c picks it). StarPiece.cpp initColors (arg3 if 0..5, else random; StarPieceDirector's pieces in turn).
# ponytail: StarPiece1_v is RAS + an environment map (no texgen here): the map's mean colour stands in (env_mean)
MAT_COLOR = {"StarPiece": ("StarPiece1_v", [(0x80, 0x00, 0x99), (0xE6, 0xA0, 0x00), (0x46, 0xA1, 0x08),
                                            (0x37, 0x5A, 0xA0), (0xBE, 0x33, 0x0B), (0x80, 0x80, 0x80)])}
# a BRK stopped at a frame the actor sets at run time: its model baked at these frames, "<class>~<k>" (game.c picks k).
# TicoFat::setScale: FatRed at (scale - 1) / 0.9 of its frame max (59); Normal frame 0 has the same colours as FatRed 0
BRK_STEPS = {"TicoFat": ("fatred", (0, 20, 39, 59))}
BRK_POSE = {"Tico": ("colorchange", 0), "TicoFat": ("normal", 0), "DinoPackunEggShellBreak": ("break", 0)}  # TicoFat: startBrk Normal, frame 0 (ponytail: FatRed as it grows)


def bva_hidden(b, frame=0):
    """Shapes a J3D visibility animation (J3D1bva1, VAF1) hides at `frame`."""
    if b[:8] != b"J3D1bva1":
        return set()
    v = b.find(b"VAF1")
    n, _, st, tt = struct.unpack(">HHII", b[v + 12:v + 24])
    out = set()
    for i in range(n):
        count, first = struct.unpack(">HH", b[v + st + 4 * i:v + st + 4 * i + 4])
        if count and not b[v + tt + first + min(frame, count - 1)]:
            out.add(i)
    return out


def hidden_joints(b, bl, files, pose=None):
    """Joints drawn at scale 0 (a joint or a parent scaled to 0): in the bind pose, or at the last frame of the BCK
    `pose` from the archive's files (FlipPanel: End and Exclamation hidden at rest, Question shown; PanelA shows
    Exclamation). Static models drop the shapes on them."""
    js = skin.joints(b, bl)
    scales = [j["scale"] for j in js]
    if pose and pose.lower() + ".bck" in files:
        bck = files[pose.lower() + ".bck"]
        clip = anim_clip(bck)
        scales = [p[0] for p in skin.clip_pose(clip, struct.unpack_from("<H", clip, 0)[0], None)]
    zero = [min(abs(x) for x in sc) < 1e-6 for sc in scales]
    out = set()
    for i, j in enumerate(js):
        k = i
        while k >= 0 and not zero[k]:
            k = js[k]["parent"]
        if k >= 0:
            out.add(i)
    return out


_ras_cache = {}


def separable(b, bl, mat, texs, over, name):
    """True when material `mat`'s TEV (at the RAS `over` sets) is its textures times a colour: the textures read 0.5
    give half what they give read 1."""
    o = dict(over or {})
    o1, oh = dict(o.get(name, {}), tex_level=1.0), dict(o.get(name, {}), tex_level=0.5)
    one = tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, dict(o, **{name: o1}))[:3]
    half = tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, dict(o, **{name: oh}))[:3]
    return bool(np.all(np.abs(half - 0.5 * one) < 0.03)) and bool(np.all(one < 0.99))


def ras_colour(b, bl, mat, texs, over, name, clr0):
    """Material `mat`'s TEV with its textures read as 1 and RAS = clr0 (quantised to 4) -> rgb 0..255."""
    q = tuple(c // 4 * 4 for c in clr0[:3])
    key = (id(b), mat, q)
    if key not in _ras_cache:
        o = dict(over or {})
        o[name] = dict(o.get(name, {}), mat=[c / 255 for c in q] + [1.0])
        _ras_cache[key] = tuple(int(c * 255 + 0.5) for c in tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, o)[:3])
    return _ras_cache[key]


def btk_entry(anim, material, mt, t, group):
    """A BTK chunk entry (after its model name) for VUMD group `group` whose material's texture matrix the BTK animates,
    or None: {u32 size, u16 group, u16 frames, u8 loop, u8 maya, 2x, f32 uv scale[2], f32 static matrix inverse[6]
    (rows), f32 centre[2], 5 x {u16 count, u16 tangent type, u16 first key, 2x}, f32 keys}. The stored UVs went through
    the static matrix and the scale: the engine undoes both (anim_btk). ponytail: vu_groups moves each triangle's UVs
    by whole texture repeats, exact only while the BTK's scale / rotation equal the static ones (scrolls; OtaKingMagma);
    a group whose BTK scales or turns its texture would need that shift left out."""
    hit = next((x for x in anim["mats"] if x[0] == material and x[1] == mt["slot"]), None)
    if not hit:
        return None
    (a, b_, c), (d, e, f) = mt["mtx"]
    det = a * e - b_ * d
    if abs(det) < 1e-8:
        return None
    ia, ib, id_, ie = e / det, -b_ / det, -d / det, a / det
    inv = (ia, ib, -(ia * c + ib * f), id_, ie, -(id_ * c + ie * f))
    tracks, keys = b"", []
    for count, tan, k in hit[3]:
        tracks += struct.pack("<HHH2x", count, tan, len(keys))
        keys += k
    body = struct.pack("<HHBB2x2f6f2f", group, anim["frames"], anim["loop"], mt.get("maya", 0), t["u_scale"], t["v_scale"],
                       *inv, *hit[2]) + tracks + struct.pack(f"<{len(keys)}f", *keys)
    return struct.pack("<I", len(body)) + body


def model_groups(arc, ratio, stats=None, skinned=False, pose=None, brk=None, mat=None):
    """-> (groups [(verts, tris, uvs or None, texture or None, slots or None, colours or None, blend)], kcl bytes or
    None,
    skin chunk entry or None) for the first J3D model of an archive. texture = tex.to_ps2 dict of the group's material
    (tex.py rule); uvs per vertex after the material's texture matrix and the mirror halving. stats counts groups
    without texture / with projected coordinates. skinned: no simplification (vertices of one group sit in different
    joint spaces), per vertex the matrix slot (PNMTXIDX / 3) and the CLR0 colour, and the SKIN entry (skin_entry).
    Static models leave out the shapes on joints hidden_joints gives (bind pose, or the BCK `pose`)."""
    groups, kcl, sk, kcls = [], None, None, {}
    pose_world = None  # POSE: joint name -> world matrix at the BCK's last frame
    stats = stats if stats is not None else {}
    files = dict(B.rarc_files(arc.read_bytes()))
    for name, b in files.items():
        n = name.lower()
        if n.endswith((".bdl", ".bmd")) and b[:4] == b"J3D2" and not groups:
            got = []
            bl = tex.blocks(b)
            hide, gtab_all, drw, posed = set(), {}, [], []
            if not skinned and b"JNT1" in bl:
                hide = hidden_joints(b, bl, {k.lower(): v for k, v in files.items()}, pose)
                gtab_all, drw = skin.group_tables(b, bl), skin.draw_entries(b, bl)
                js = skin.joints(b, bl)
                wj = skin.world(js)  # bind pose
                if arc.stem in POSE:  # the BCK's last frame (POSE)
                    clip = anim_clip({k.lower(): v for k, v in files.items()}[POSE[arc.stem][0].lower() + ".bck"])
                    lp = skin.clip_pose(clip, struct.unpack_from("<H", clip, 0)[0], None)
                    wj = skin.world(js, [skin.local(sc, ro, tr) for sc, ro, tr in lp])
                    pose_world = {j["name"]: w for j, w in zip(js, wj)}
                posed = skin.draw_mtx(drw, wj)  # per DRW1 entry
            # ponytail: models whose draw matrices move vertices are not simplified (the simplifier sees positions
            # before posing: LavaGeyser's rings, all at 0, came out mangled); posing inside geom.shp1 if it matters
            moved = any(not np.allclose(m, np.eye(4), atol=1e-3) for m in posed)
            geom.model(b, 1.0 if skinned or moved else ratio, None, got)
            clr = geom.vtx1_color(b, bl[b"VTX1"])
            nget = geom.vtx1_attr(b, bl[b"VTX1"], 10)
            if skinned:
                tables = skin.group_tables(b, bl)
                gtab = []
            shapes = tex.shape_materials(b, bl[b"INF1"])
            texs = tex.textures(b, bl[b"TEX1"]) if b"TEX1" in bl else []
            ps2, gains, ras_split = {}, {}, {}
            sky = arc.stem in SKY_ARCS or arc.stem.endswith("Sky")  # 8-bit textures (tex.to_ps2 hi)
            lfiles = {k.lower().rsplit("/", 1)[-1]: v for k, v in files.items()}
            bp = brk or BRK_POSE.get(arc.stem)  # a BRK_ARG variant's (name, frame) first
            over = tex.brk(lfiles[bp[0] + ".brk"], bp[1]) if bp and bp[0] + ".brk" in lfiles else None
            if mat:  # MAT_COLOR: (material, rgb)
                over = dict(over or {})
                over[mat[0]] = dict(over.get(mat[0], {}), mat=[c / 255 for c in mat[1]] + [1.0], env_mean=True)
            # MapObjActor::init (tryStartAllAnim(mObjectName)), OtaKingMagma (startBtk): the model's own-name BTK, played
            # at run time (anim_btk). ponytail: every model's, not only those classes'
            anim_tex = tex.btk(lfiles[arc.stem.lower() + ".btk"]) if arc.stem.lower() + ".btk" in lfiles else None
            bva, vframe = (pose, None) if pose and pose.lower() + ".bva" in lfiles else VIS.get(arc.stem, (None, None))
            vis_hide = bva_hidden(lfiles.get(bva.lower() + ".bva", b""), 1 << 30 if vframe is None else vframe) if bva else set()
            for pos, tri, info in got:
                if not tri or info["shape"] in vis_hide:
                    continue
                ents = [drw[d] for d in gtab_all.get((info["shape"], info["group"]), []) if d < len(drw)]
                if hide and ents and all(m is None and j in hide for j, m in ents):
                    continue
                used = sorted(set(tri))
                remap = {v: i for i, v in enumerate(used)}
                verts = [pos[3 * v:3 * v + 3] for v in used]
                # static: each vertex through its draw matrix (rigid shapes keep joint-local positions: LavaGeyser's
                # rings sit at their joints), the PNMTXIDX slot of the group's DRW1 table
                mats = None
                tab = gtab_all.get((info["shape"], info["group"]), [])
                if posed and tab:
                    at0 = info["at"].get(0)
                    d = [tab[min(info["keys"][v][at0[0]] // 3 if at0 else 0, len(tab) - 1)] for v in used]
                    mats = [posed[k] if k < len(posed) else np.eye(4) for k in d]
                    verts = [tuple(float(x) for x in (m[:3, :3] @ p + m[:3, 3])) for m, p in zip(mats, verts)]
                mat = shapes.get(info["shape"])
                mt = tex.material_tex(b, bl[b"MAT3"], mat, bl.get(b"TEX1")) if mat is not None else None
                get = geom.vtx1_attr(b, bl[b"VTX1"], mt["attr"]) if mt and mt["attr"] in info["at"] else None
                uvs = t = None
                gain, split = 1, True
                sea_add = False
                get0 = geom.vtx1_attr(b, bl[b"VTX1"], 13) if 13 in info["at"] else None  # TEX0, for SKY_SEA's base
                # a material whose rasterized colour is CLR0 (its TEV may use RAS other than as a factor: PeanutPlanet's
                # TEX x clamp(4 lerp(K, C0, RAS)), TicoFat's lerp(C1, TEX, RAS) + C0): per vertex the TEV with the
                # texture read as 1 at its own CLR0 (ras_colour), the texture the TEV at the group's mean CLR0 divided
                # by that factor there (exact when the TEV is TEX x f(RAS), near the mean otherwise)
                ras_mean = None
                if clr and 11 in info["at"] and mat is not None and tex.vertex_colour(b, bl[b"MAT3"], mat):
                    o11, w11 = info["at"][11]
                    vc = [clr(info["keys"][v][o11] if w11 == 1 else info["keys"][v][o11] << 8 | info["keys"][v][o11 + 1])
                          for v in used]
                    ras_mean = tuple(max(1, round(sum(c[k] for c in vc) / len(vc) / 8) * 8) for k in range(3))
                # an environment-mapped texture (texgen from normals, tex.env_mtx): baked as any, its UVs made on VU1
                # from the view-space normal (VU1Env)
                env = mt["env"] if mt and mt.get("env") and nget and 10 in info["at"] and not get else None
                # an environment map added over a textured material (tex.env_layer: Dino, Tico's eyes): the base baked
                # without it, the layer a second, additive group (after this one)
                layer = (tex.env_layer(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, over)
                         if get and mat is not None and nget and 10 in info["at"] else None)
                if (get or env) and mt["tex"] < len(texs):
                    key = (mt["tex"], mat, ras_mean)  # the material's TEV baked into its texture (tex.tev)
                    if key not in ps2:
                        x = texs[mt["tex"]]
                        binfo = {}
                        o2 = over
                        if layer:  # its env layer drawn apart: read as 0 here
                            o2 = dict(o2 or {})
                            o2[layer["name"]] = dict(o2.get(layer["name"], {}), zero_coord=layer["coord"])
                        if ras_mean:
                            mn = tex.material_name(b, bl[b"MAT3"], mat)
                            o2 = dict(over or {})
                            o2[mn] = dict(o2.get(mn, {}), mat=[c / 255 for c in ras_mean] + [1.0])
                        baked = tex.bake(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, mt["tex"], o2, binfo)
                        if ras_mean and not separable(b, bl, mat, texs, o2, mn):
                            ras_split[key] = False  # not TEX x f(RAS): baked at the mean, vertices CLR0 / mean
                        elif ras_mean:
                            ras_split[key] = True
                            g0 = np.maximum(tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, o2)[:3], 1 / 255)
                            baked = baked.copy()
                            baked[..., :3] = np.clip(baked[..., :3] / g0 + 0.5, 0, 255).astype(np.uint8)
                        ps2[key] = tex.to_ps2(baked, x["wrap_s"], x["wrap_t"], sky)
                        gains[key] = binfo.get("gain", 1)
                    t = ps2[key]
                    gain = gains[key]
                    split = ras_split.get(key, True)
                    if (arc.stem == "Mario" and tex.material_name(b, bl[b"MAT3"], mat) == "EyeLid_v" and
                            mt["tex"] + 3 < len(texs)):
                        # MarioActor::swapTextureInit: mario_eyeLid.0 .. .2 (open, half, shut), the blink's
                        # (updateBlink) swaps, baked through the same material (SWAP chunk)
                        alts = []
                        for d in (1, 2, 3):
                            x2 = texs[mt["tex"] + d]
                            alts.append(tex.to_ps2(tex.bake(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, mt["tex"] + d,
                                                            over, {}), x2["wrap_s"], x2["wrap_t"], sky))
                        t = dict(t, alts=alts)
                if env and mt["tex"] < len(texs):  # rows s, t times the texture's UV scales
                    t = dict(t, env=[e * (t["u_scale"], t["v_scale"], 1.0)[i % 3] for i, e in enumerate(env)])
                    uvs = [(0.0, 0.0)] * len(used)
                    stats["env"] = stats.get("env", 0) + 1
                elif get and mt["tex"] < len(texs):
                    off, w = info["at"][mt["attr"]]
                    m = mt["mtx"]
                    uvs = []
                    for v in used:
                        k = info["keys"][v]
                        su, sv = get(k[off] if w == 1 else k[off] << 8 | k[off + 1])
                        if m:
                            su, sv = (m[0][0] * su + m[0][1] * sv + m[0][2], m[1][0] * su + m[1][1] * sv + m[1][2])
                        uvs.append((su * t["u_scale"], sv * t["v_scale"]))
                    stats["projected"] = stats.get("projected", 0) + mt["projected"]
                    if anim_tex and m:
                        e = btk_entry(anim_tex, tex.material_name(b, bl[b"MAT3"], mat), mt, t, len(groups))
                        if e:
                            stats.setdefault("_btk", []).append(e)
                elif (mt and mt["projected"] and arc.stem in SKY_ARCS and mt["tex"] < len(texs) and
                      tex.material_name(b, bl[b"MAT3"], mat) in SKY_SEA and get0 is not None):
                    # VROrbit's sea (EarthNightMat_v floor, EarthFar_v wall): base + CLR0 x Cloud01k on the Wii's
                    # Projmap UVs (tex.projmap, camera at the sky's origin): the base a group of its own, the clouds
                    # this one, added (blend 2), coloured by CLR0. ponytail: EarthKsMM (CLR0 x its corner glow) left
                    # out, uv = st / q per vertex (the wall's q changes across a triangle)
                    sea = SKY_SEA[tex.material_name(b, bl[b"MAT3"], mat)]
                    pm = tex.projmap(b, bl[b"MAT3"], mat, mt["slot"])
                    x = texs[mt["tex"]]
                    key = ("sea", mt["tex"])
                    if key not in ps2:
                        ps2[key] = tex.to_ps2(x["rgba"], x["wrap_s"], x["wrap_t"], sky)
                    t = ps2[key]
                    uvs = []
                    for p in verts:
                        s_, t_, q_ = pm @ np.array([p[0], p[1], p[2], 1.0])
                        q_ = q_ if abs(q_) > 1e-6 else 1e-6
                        uvs.append((s_ / q_ * t["u_scale"], t_ / q_ * t["v_scale"]))
                    ras_mean, sea_add = None, True
                    if anim_tex:  # VROrbit.btk: the clouds' SRT; its translation scrolls them (anim_btk)
                        e = btk_entry(anim_tex, tex.material_name(b, bl[b"MAT3"], mat),
                                      dict(mt, mtx=tex.projmap(b, bl[b"MAT3"], mat, mt["slot"], True)), t,
                                      len(groups) + 1)  # after its base group
                        if e:
                            stats.setdefault("_btk", []).append(e)
                    if sea[1] is None:  # the floor: K0 flat
                        groups.append((verts, [remap[v] for v in tri], None, None, None, sea[0], (0, 1, 255), False, 1))
                    else:  # the wall: lerp(C1, K1, EarthFarK) on its own UVs (TEX0 through texture matrix 1)
                        bt, bslot, c1, k1 = sea[1:]
                        bkey = ("seabase", bt)
                        if bkey not in ps2:
                            y = texs[bt]["rgba"].astype(np.float32) / 255
                            img = np.array(c1 + (255,), np.float32) / 255 + (np.array(k1 + (255,), np.float32) / 255 -
                                                                         np.array(c1 + (255,), np.float32) / 255) * y
                            img[..., 3] = 1.0
                            ps2[bkey] = tex.to_ps2((img * 255 + 0.5).astype(np.uint8), texs[bt]["wrap_s"], texs[bt]["wrap_t"], sky)
                        bt2 = ps2[bkey]
                        bm = tex.mtx_slot(b, bl[b"MAT3"], mat, bslot)
                        o0, w0 = info["at"][13]
                        buv = []
                        for v in used:
                            k = info["keys"][v]
                            su, sv = get0(k[o0] if w0 == 1 else k[o0] << 8 | k[o0 + 1])
                            if bm:
                                su, sv = (bm[0][0] * su + bm[0][1] * sv + bm[0][2], bm[1][0] * su + bm[1][1] * sv + bm[1][2])
                            buv.append((su * bt2["u_scale"], sv * bt2["v_scale"]))
                        groups.append((verts, [remap[v] for v in tri], buv, bt2, None, None, (0, 1, 255), False, 1))
                elif mt and mt["projected"] and arc.stem in SKY_ARCS and mt["tex"] < len(texs):
                    # ponytail: a sky's texture projected from vertex positions (texgen POS + texture matrix) gets
                    # spherical UVs from each vertex's direction instead (the stars tile SKY_TILES times around)
                    x = texs[mt["tex"]]
                    key = (mt["tex"], mat)
                    if key not in ps2:
                        baked = tex.bake(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, mt["tex"])
                        ps2[key] = tex.to_ps2(baked, x["wrap_s"], x["wrap_t"], sky)
                    t = ps2[key]
                    uvs = []
                    for vx, vy, vz in verts:
                        l = math.sqrt(vx * vx + vy * vy + vz * vz) or 1.0
                        su = (math.atan2(vz, vx) / (2 * math.pi) + 0.5) * SKY_TILES
                        sv = (math.asin(max(-1.0, min(1.0, vy / l))) / math.pi + 0.5) * SKY_TILES / 2
                        uvs.append((su * t["u_scale"], sv * t["v_scale"]))
                else:
                    stats["untextured"] = stats.get("untextured", 0) + 1
                if t is None:
                    ras_mean = None  # untextured: the flat colour times CLR0 as before
                slots = cols = None
                if t is None and mat is not None and not skinned:  # untextured: the TEV's constant colour
                    cols = tuple(int(c * 255 + 0.5) for c in tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, over)[:3])
                if not skinned and clr and 11 in info["at"] and mat is not None and tex.vertex_colour(b, bl[b"MAT3"], mat):
                    # unlit, coloured per vertex (VROrbit's cloud layers fade to black): CLR0 times the flat colour
                    o, w = info["at"][11]
                    flat = cols or (255, 255, 255)
                    cols = [tuple(f * c // 255 for f, c in zip(flat, clr(info["keys"][v][o] if w == 1 else
                                                                          info["keys"][v][o] << 8 | info["keys"][v][o + 1])))
                            for v in used]
                if skinned:
                    at = info["at"]
                    slots = [info["keys"][v][at[0][0]] // 3 if 0 in at else 0 for v in used]
                    # untextured: the TEV's constant colour (Tico's yellow), times CLR0 when it has one
                    flat = (tuple(int(c * 255 + 0.5) for c in tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, over)[:3])
                            if t is None and mat is not None else None)
                    if clr and 11 in at:
                        o, w = at[11]
                        cols = [clr(info["keys"][v][o] if w == 1 else info["keys"][v][o] << 8 | info["keys"][v][o + 1])
                                for v in used]
                        if flat:
                            cols = [tuple(f * c // 255 for f, c in zip(flat, cv[:3])) + tuple(cv[3:]) for cv in cols]
                    elif flat:
                        cols = [flat] * len(used)
                    gtab.append(tables[(info["shape"], info["group"])])
                blend = tex.material_blend(b, bl[b"MAT3"], mat) if mat is not None else (0, 1, 255)
                if sea_add:
                    blend = (2, 0, 255)
                if mat is not None and tex.samples_named(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), "IndDummy"):
                    # the screen behind, through its TEV's tint (CrystalCage's IndTex refracts a screen copy): kind 3,
                    # the frame buffer times the tint's mean. ponytail: no bend (indirect texture), one grey tint
                    tint = tex.tev(b, bl[b"MAT3"], mat, bl.get(b"TEX1"), texs, None, over)[:3]
                    blend = (3, blend[1], max(0, min(255, round(255 * float(sum(tint)) / 3))))
                    t, uvs, cols = None, None, None if skinned else (255, 255, 255)
                # per-vertex normals for lit materials (lit by the engine, vu_groups), False for unlit ones, None: lit
                # without normals (face normals)
                nrm = None
                if (mat is None or not tex.lit(b, bl[b"MAT3"], mat)) and not (t and t.get("env")):
                    nrm = False
                elif nget and 10 in info["at"]:
                    o, w = info["at"][10]
                    nrm = [nget(info["keys"][v][o] if w == 1 else info["keys"][v][o] << 8 | info["keys"][v][o + 1])
                           for v in used]
                    if mats:
                        nrm = [tuple(float(x) for x in skin.unit(m[:3, :3] @ n)) for m, n in zip(mats, nrm)]
                if ras_mean and isinstance(cols, list) and split:  # per vertex the TEV's factor at its CLR0 (ras_colour)
                    mn = tex.material_name(b, bl[b"MAT3"], mat)
                    cols = [ras_colour(b, bl, mat, texs, over, mn, cv) + tuple(c[3:]) for cv, c in zip(vc, cols)]
                elif ras_mean and isinstance(cols, list):  # baked at the mean: CLR0 / mean left (at most 1)
                    cols = [tuple(min(255, c[k] * 255 // ras_mean[k]) for k in range(3)) + tuple(c[3:]) for c in cols]
                groups.append((verts, [remap[v] for v in tri], uvs, t, slots, cols, blend, nrm, gain))
                if layer and t is not None and layer["tex"] < len(texs):
                    lkey = ("layer", layer["tex"], mat)
                    if lkey not in ps2:
                        x = texs[layer["tex"]]
                        lt = x["rgba"].astype(np.float32)
                        lt[..., :3] = np.clip(lt[..., :3] * layer["k"], 0, 255)
                        lt[..., 3] = 255  # added whole (K x TEXC): no texture alpha, the blend's FIX 1
                        ps2[lkey] = tex.to_ps2(lt.astype(np.uint8), x["wrap_s"], x["wrap_t"], sky)
                    lt = ps2[lkey]
                    lt = dict(lt, env=[e * (lt["u_scale"], lt["v_scale"], 1.0)[i % 3] for i, e in enumerate(layer["env"])])
                    ln = nrm if isinstance(nrm, list) else None
                    if ln is None:  # an unlit material's normals, for the layer only
                        o, w = info["at"][10]
                        ln = [nget(info["keys"][v][o] if w == 1 else info["keys"][v][o] << 8 | info["keys"][v][o + 1])
                              for v in used]
                        if mats:
                            ln = [tuple(float(x) for x in skin.unit(m_[:3, :3] @ n)) for m_, n in zip(mats, ln)]
                    groups.append((verts, [remap[v] for v in tri], [(0.0, 0.0)] * len(used), lt, slots, None,
                                   (2, 0, 255), ln, 1))
                    if skinned:
                        gtab.append(tables[(info["shape"], info["group"])])
                    stats["env_layer"] = stats.get("env_layer", 0) + 1
            if skinned:
                sk = skin_entry(b, bl, gtab)
        elif n.endswith(".kcl"):
            kcls[n.split("/")[-1]] = b
    for kn in list(kcls):  # each prism's attribute resolved through its .pa (kcl_codes)
        kcls[kn] = kcl_codes(kcls[kn], next((v for k, v in files.items() if k.lower().split("/")[-1] == kn[:-4] + ".pa"), b""))
    # the model's own collision: <archive>.kcl, else the first that is not a MoveLimit boundary (PuffballPlanetA lists
    # movelimit.kcl first: its ground was a missing invisible wall)
    kcl = kcls.get(arc.stem.lower() + ".kcl") or next((v for k, v in kcls.items() if not k.startswith("movelimit")), None)
    if kcl and arc.stem in POSE and pose_world:  # its parts' collisions at their posed joints, one KCL
        tris = kcl_tris(kcl)
        for kn, jn in POSE[arc.stem][1].items():
            w = pose_world[jn]
            tris += [tuple(w[:3, :3] @ v + w[:3, 3] for v in t[:3]) + (t[3],) for t in kcl_tris(kcls[kn])]
        kcl = kcl_build(tris, struct.unpack_from(">f", kcl, 0x10)[0])
    if kcl and arc.stem in KCL_JOINT:  # collision placed at a joint (initCollisionParts with its joint matrix)
        mb = next(v for k, v in files.items() if k.lower().endswith((".bdl", ".bmd")))
        bl = tex.blocks(mb)
        js = skin.joints(mb, bl)
        w = skin.world(js)[[j["name"] for j in js].index(KCL_JOINT[arc.stem])]
        kcl = kcl_shift(kcl, [float(x) for x in w[:3, 3]])
    return groups, kcl, sk


def skin_entry(b, bl, gtab):
    """SKIN chunk entry (after the model name): u16 njoints, ndrw, ngroups, nmix; joints {s16 parent, u16 pad (1: the Move joint),
    f32 scale[3], rot[3] (radians), trans[3]} (bind pose locals); drw {u16 first, u16 count} (count 0: rigid, first =
    joint; else a run of mix entries); mix {u16 joint, u16 pad, f32 weight, f32 invbind[3][4]}; groups {u8 nmat,
    u8 drw[11]} (skin.py)."""
    js, dr = skin.joints(b, bl), skin.draw_entries(b, bl)
    out, drw, mix = bytearray(), bytearray(), bytearray()
    nmix = 0
    for j, m in dr:
        if m is None:
            drw += struct.pack("<HH", j, 0)
        else:
            drw += struct.pack("<HH", nmix, len(m))
            for jj, w, ib in m:
                mix += struct.pack("<HHf12f", jj, 0, w, *ib[:3, :].reshape(-1))
                nmix += 1
    assert all(len(t) <= 11 and max(t) < 256 for t in gtab), "matrix table does not fit u8[11]"
    out += struct.pack("<4H", len(js), len(dr), len(gtab), nmix)
    for j in js:
        out += struct.pack("<hH9f", j["parent"], j["name"] == "Move", *j["scale"], *j["rot"], *j["trans"])  # pad 1: MapObjActor's collision joint
    out += drw + mix
    for t in gtab:
        out += struct.pack("<B11B", len(t), *(list(t) + [0] * (11 - len(t))))
    return bytes(out) + b"\0" * (-len(out) % 4)


def anim_clip(bck):
    """BCK (ANK1) -> little-endian clip: {u16 frame_max, u8 attribute (loop mode), u8 dec_shift, u16 njoints,
    u16 nscale, u16 nrot, u16 ntrans, 4 pad}, njoints x 3 axes x {scale, rot, trans} x {u16 count, u16 offset,
    u16 type} (J3DAnmTransformKeyTable), then f32 scale[nscale], s16 rot[nrot] (pad to 4), f32 trans[ntrans].
    Offsets in the ANK1 block (J3DAnmLoader.cpp:303-313, J3DAnimation.hpp:306-317) are from the block start."""
    k = 0x20
    assert bck[k:k + 4] == b"ANK1", bck[k:k + 4]
    attr, shift = bck[k + 8], bck[k + 9]
    fmax, nj, ns, nr, nt = struct.unpack_from(">h4H", bck, k + 0xA)
    tab, so, ro, to = (k + B.u32(bck, k + x) for x in (0x14, 0x18, 0x1C, 0x20))
    table = struct.unpack_from(f">{nj * 27}H", bck, tab)
    out = struct.pack("<HBBHHHH4x", fmax, attr, shift, nj, ns, nr, nt) + struct.pack(f"<{nj * 27}H", *table)
    out += b"\0" * (-len(out) % 4)
    out += struct.pack(f"<{ns}f", *struct.unpack_from(f">{ns}f", bck, so))
    rot = struct.pack(f"<{nr}h", *struct.unpack_from(f">{nr}h", bck, ro))
    out += rot + b"\0" * (-len(rot) % 4)
    out += struct.pack(f"<{nt}f", *struct.unpack_from(f">{nt}f", bck, to))
    return out


def kcl_codes(b, pa):
    """Big-endian KCL with each prism's attribute (a row of its .pa, KCollisionServer / CollisionCode.cpp) replaced by
    that row's codes: Floor_code in bits 0-5 (34 the most in the game), Wall_code in 6-9, Camera_through in bit 10,
    Sound_code in 11-14 (15 the most; game.c KCL_SOUND). A KCL without a .pa keeps 0 (Normal)."""
    out = bytearray(b)
    cols = [B.bcsv_column(pa, f) for f in ("Floor_code", "Wall_code", "Camera_through", "Sound_code")]
    pos, nrm, prism, octree = struct.unpack_from(">4I", b, 0)
    for o in range(prism + 0x10, octree, 0x10):
        at = struct.unpack_from(">H", b, o + 0xE)[0]
        f, w, c, snd = (col[at] if at < len(col) and isinstance(col[at], int) else 0 for col in cols)
        struct.pack_into(">H", out, o + 0xE, (f & 0x3F) | (w & 0xF) << 6 | (c & 1) << 10 | (snd & 0xF) << 11)
    return bytes(out)


def kcl_tris(b):
    """Big-endian KCL -> [(a, b, c, attribute)] (numpy vertices), each prism back to its triangle: a its position, the
    other two where the edges through a (normals n0, n1, in the face plane) meet the opposite edge (n2, at height)."""
    pos, nrm, prism, octree = struct.unpack_from(">4I", b, 0)
    v = lambda off, i: np.array(struct.unpack_from(">3f", b, off + 12 * i))
    out = []
    for o in range(prism + 0x10, octree, 0x10):
        h, pi, fi, e0, e1, e2, at = struct.unpack_from(">f6H", b, o)
        a, fn, n2 = v(pos, pi), v(nrm, fi), v(nrm, e2)
        ends = []
        for e in (e0, e1):
            d = np.cross(v(nrm, e), fn)
            k = d @ n2
            if abs(k) < 1e-9:
                break
            ends.append(a + d * (h / k))
        if len(ends) == 2:
            out.append((a, ends[1], ends[0], at))  # n0's edge is a-c, n1's a-b (the face normal (b-a) x (c-a))
    return out


def kcl_build(tris, thickness=40.0):
    """[(a, b, c, attribute)] -> big-endian KCL (the layout kcl_to_le reads): one root block (xshift = xyshift = -1)
    whose list holds every prism; edge normals point out (n0 edge a-c, n1 edge a-b, n2 edge b-c at height). ponytail:
    no octree subdivision, for small collisions (a joint's part); split blocks if a big one is ever built this way"""
    pts, nrms, prisms = [], [], []
    def idx(lst, x):
        lst.append(tuple(float(c) for c in x))
        return len(lst) - 1
    for a, b, c, at in tris:
        a, b, c = (np.asarray(x, float) for x in (a, b, c))
        fn = np.cross(b - a, c - a)
        if np.linalg.norm(fn) < 1e-6:
            continue
        fn /= np.linalg.norm(fn)
        n0, n1, n2 = (x / np.linalg.norm(x) for x in (np.cross(fn, c - a), np.cross(b - a, fn), np.cross(c - b, fn)))
        prisms.append((float((b - a) @ n2), idx(pts, a), idx(nrms, fn), idx(nrms, n0), idx(nrms, n1), idx(nrms, n2), at))
    allp = np.array([x for t in tris for x in t[:3]], float)
    lo = allp.min(0) - 1.0
    ext = float((allp.max(0) + 1.0 - lo).max())
    shift = max(1, int(math.ceil(math.log2(max(ext, 2.0)))))
    mask = ~((1 << shift) - 1)
    hdr = 0x38
    pos_off = hdr
    nrm_off = pos_off + 12 * len(pts)
    prism_off = nrm_off + 12 * len(nrms) - 0x10  # prisms are 1-based: index 0 sits at prismOffset
    body = b"".join(struct.pack(">3f", *x) for x in pts) + b"".join(struct.pack(">3f", *x) for x in nrms)
    body += b"".join(struct.pack(">f6H", *pr) for pr in prisms)
    oct_off = hdr + len(body)
    lst = struct.pack(f">{len(prisms) + 2}H", 0, *range(1, len(prisms) + 1), 0)  # first u16 never read, 0 ends it
    octree = struct.pack(">I", 0x80000000 | 4) + lst  # the root: a list 4 bytes on (offset from the node)
    head = struct.pack(">4If3f6i", pos_off, nrm_off, prism_off, oct_off, thickness, *lo, mask, mask, mask, shift, -1, -1)
    return head + body + octree


def kcl_selftest():
    tris = [((0, 0, 0), (100, 0, 0), (0, 0, -100), 3), ((0, 0, 0), (0, 0, -100), (-100, 50, 0), 1)]
    back = kcl_tris(kcl_build(tris))
    assert len(back) == 2
    for (a, b, c, at), (x, y, z, w) in zip(tris, back):
        assert at == w and np.allclose(a, x, atol=1e-3)
        assert {tuple(np.round(b, 2)), tuple(np.round(c, 2))} == {tuple(np.round(y, 2)), tuple(np.round(z, 2))}
    print("kcl selftest ok")


def kcl_shift(b, d):
    """Big-endian KCL moved by d: its positions and its octree's min corner (the octree is relative to min)."""
    out = bytearray(b)
    pos, nrm = struct.unpack_from(">2I", b, 0)
    for o in list(range(pos, nrm, 12)) + [0x14]:
        struct.pack_into(">3f", out, o, *(a + c for a, c in zip(struct.unpack_from(">3f", b, o), d)))
    return bytes(out)


def kcl_to_le(b):
    """KCL big-endian -> little-endian, same layout (Petari KCollision.hpp:9-41, KCollision.cpp searchBlock):
    header 14 words; pos and normal arrays (f32); prisms from prismOffset+0x10 (1-based), 0x10 each {f32 height,
    u16 pos, nrm, edge0..2, attribute}; octree: a root grid of s32, each >= 0 = offset (from the current node) to 8
    child s32, < 0 = (value & 0x7FFFFFFF) offset to a u16 prism list whose entries after the first run until 0."""
    out = bytearray(b)
    sw32 = lambda o: out.__setitem__(slice(o, o + 4), b[o:o + 4][::-1])
    sw16 = lambda o: out.__setitem__(slice(o, o + 2), b[o:o + 2][::-1])
    pos, nrm, prism, octree = struct.unpack_from(">4I", b, 0)
    xmask, ymask, zmask, shift, xshift, xyshift = struct.unpack_from(">6i", b, 0x20)
    for o in range(0, 0x38, 4):
        sw32(o)
    for o in range(pos, prism + 0x10, 4):   # positions then normals, all f32
        sw32(o)
    for o in range(prism + 0x10, octree, 0x10):
        sw32(o)
        for k in range(6):
            sw16(o + 4 + 2 * k)
    blocks = lambda m: ((~m & 0xFFFFFFFF) >> shift) + 1   # root cells along an axis (mask = ~(extent - 1))
    root = 1 if xshift == -1 and xyshift == -1 else blocks(xmask) * blocks(ymask) * blocks(zmask)
    seen_nodes, seen_lists, seen_words = set(), set(), set()
    todo = [(octree, root)]
    while todo:
        base, count = todo.pop()
        if base in seen_nodes:
            continue
        seen_nodes.add(base)
        for i in range(count):
            e = base + 4 * i
            v = struct.unpack_from(">i", b, e)[0]
            if e not in seen_words:
                seen_words.add(e)
                sw32(e)
            if v >= 0:
                todo.append((base + v, 8))
            else:
                # a list's first u16 is never read (KCollision.cpp: while (*++prismList)) and may be the low half of
                # a node word (AssemblyBlockPartsB): left alone. Lists overlap, so each u16 swaps once
                o = base + (v & 0x7FFFFFFF) + 2
                while True:
                    if o not in seen_lists:
                        seen_lists.add(o)
                        sw16(o)
                    if struct.unpack_from(">H", b, o)[0] == 0:
                        break
                    o += 2
    return bytes(out)


LIGHT_TYPES = json.loads((Path(__file__).resolve().parent / "light_types.json").read_text())
BASE_COLOUR = {"Coin": (240, 208, 48), "PurpleCoin": (176, 80, 240), "StarPiece": (96, 208, 240),
               "Kuribo": (144, 80, 32)}


# pixels per world unit at depth 1: render.c's fovy 45 over 448 lines (f = 224 / tan 22.5), geometric mean of the
# horizontal and the half-height vertical scale (224 drawn lines)
LOD_F = 224 / math.tan(math.radians(22.5)) / math.sqrt(2)


def lod_k(sums):
    """GS TEX1 K (1/16 units) of a texture from lod_sums over every group using it: the GS takes LOD = log2(depth) +
    K, and texels per pixel = density * depth / LOD_F, density = area-weighted texels per model unit (ponytail:
    instance scale 1, fovy 45, one K per texture)."""
    num, den = sums
    if not num or not den:
        return 0
    return max(-2048, min(2047, round(16 * math.log2(num / den / LOD_F))))


def lod_sums(verts, tris, uvs, t):
    """(sum of sqrt(texel area * model area), sum of model area) over the triangles: lod_k's density terms."""
    num = den = 0.0
    for i in range(0, len(tris), 3):
        a, b, c = (verts[tris[i + k]] for k in range(3))
        e1, e2 = [b[j] - a[j] for j in range(3)], [c[j] - a[j] for j in range(3)]
        pa = math.sqrt(sum(x * x for x in (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                                           e1[0] * e2[1] - e1[1] * e2[0]))) / 2
        (u0, v0), (u1, v1), (u2, v2) = (uvs[tris[i + k]] for k in range(3))
        ua = abs((u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0)) / 2 * t["w"] * t["h"]
        num += math.sqrt(ua * pa)
        den += pa
    return num, den


UV_ONE = 4096  # UVs as s16 with 12 fraction bits (VU1 itof12): -8 .. +8 per triangle


def pos_scale(groups):
    """Quantisation step of a model's s16 positions: the largest coordinate maps to 32767."""
    m = max((abs(c) for verts, *_ in groups for v in verts for c in v), default=1.0)
    return (m or 1.0) / 32767.0


def vu_groups(groups, base, stats=None, qs=1.0):
    """Indexed groups -> [(pos s16x4, colour u8x4, uv s16x2, normal s8x4 or b"")] per group. Lit materials (normals
    list, or None: face normals) are lit on VU1 (render.c): their colour is stored x gain / 2 (the engine multiplies
    by 2 x the light, clamped at 2) with the unit normal x 127 per vertex; unlit ones keep k = 1.
    Positions are x, y, z in steps of qs (the engine folds qs into the matrix) and w = 7 * matrix slot (0: static).
    Textured groups get grey 0x80 * k (the GS modulates: 0x80 = 1.0), untextured ones base * k. Each triangle's UVs
    are shifted by whole repeats towards 0 on repeating axes so they fit s16; triangles still wider than 8 repeats
    are clamped and counted in stats["uv_clamped"]. Colour and UV arrays are padded to 16 bytes (DMA)."""
    pad = lambda x: bytes(x) + b"\0" * (-len(x) % 16)
    out = []
    for verts, tris, uvs, t, slots, cols, _, nrm, gain in groups:
        pos, col, uv, nb = bytearray(), bytearray(), bytearray(), bytearray()
        rgb = (0x80, 0x80, 0x80) if t else cols if slots is None and isinstance(cols, tuple) else base
        vcol = slots is None and isinstance(cols, list)  # unlit vertex colours (0..255; textured: times 0x80 / 255)
        env = bool(t and t.get("env"))  # environment-mapped: its normals, unlit (VU1Env)
        lit = nrm is not False and not env
        lk = gain / 2.0 if lit else 1.0  # lit: stored x gain / 2 (VU1 multiplies by 2 x the light)
        for i in range(0, len(tris), 3):
            a, b, c = (verts[tris[i + k]] for k in range(3))
            e1 = [b[j] - a[j] for j in range(3)]
            e2 = [c[j] - a[j] for j in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            nl = math.sqrt(sum(x * x for x in n)) or 1.0
            rgba = struct.pack("<4B", *(min(255, int(x * lk)) for x in rgb), 0x80)
            for q in range(3):
                vi = tris[i + q]
                v = verts[vi]
                p = [max(-32767, min(32767, round(c / qs))) for c in v]
                if slots is None:   # static: slot 0
                    pos += struct.pack("<4h", *p, 0)
                    col += struct.pack("<4B", *(min(255, int((x * 0x80 // 255 if t else x) * lk)) for x in cols[vi][:3]),
                                       0x80) if vcol else rgba
                else:               # skinned: w = 7 * matrix slot; colour = CLR0 scaled to 0x80
                    pos += struct.pack("<4h", *p, 7 * slots[vi])
                    cc = cols[vi] if cols else (255, 255, 255, 255)
                    col += struct.pack("<4B", *(min(255, int(x * 0x80 // 255 * lk)) for x in cc[:3]), 0x80)
                if lit or env:
                    nv = nrm[vi] if nrm else [x / nl for x in n]
                    ln = math.sqrt(sum(x * x for x in nv)) or 1.0
                    nb += struct.pack("<4b", *(max(-127, min(127, round(127 * x / ln))) for x in nv), 0)
            if uvs:
                tri = [uvs[tris[i + k]] for k in range(3)]
                du = 0 if t["clamp_s"] else math.floor(min(x[0] for x in tri))
                dv = 0 if t["clamp_t"] else math.floor(min(x[1] for x in tri))
                for su, sv in tri:
                    q = [round((su - du) * UV_ONE), round((sv - dv) * UV_ONE)]
                    if not all(-32768 <= x < 32768 for x in q) and stats is not None:
                        stats["uv_clamped"] = stats.get("uv_clamped", 0) + 1
                    uv += struct.pack("<2h", *(max(-32768, min(32767, x)) for x in q))
        out.append((pad(pos), pad(col), pad(uv), bytes(nb)))
    return out


BATCH = 60  # emitted vertices per VU1 batch (engine render.c BATCH)


def index_batches(pos, col, uv, n, nrm=b""):
    """De-indexed group arrays (vu_groups) -> (batch table, data) of the indexed VUMD layout: per batch of BATCH
    emitted vertices, the unique (position, uv, normal) triples once, then per emitted vertex its colour with the
    unique index in the 4th byte (the alpha, always 0x80, is put back on VU1). -> (nbatch, bytes)."""
    pad = lambda x: bytes(x) + b"\0" * (-len(x) % 16)
    table, data = b"", b""
    for o in range(0, n, BATCH):
        ne = min(BATCH, n - o)
        keys, upos, uuv, unrm, cols = {}, bytearray(), bytearray(), bytearray(), bytearray()
        for e in range(o, o + ne):
            p = pos[8 * e:8 * e + 8]
            t = uv[4 * e:4 * e + 4] if uv else b""
            r = nrm[4 * e:4 * e + 4] if nrm else b""
            if p + t + r not in keys:
                keys[p + t + r] = len(keys)
                upos += p
                uuv += t
                unrm += r
            k = keys[p + t + r]
            cols += col[4 * e:4 * e + 3] + bytes((k,))
        table += struct.pack("<HH", ne, len(keys))
        data += pad(upos) + (pad(uuv) if uv else b"") + (pad(unrm) if nrm else b"") + pad(cols)
    return len(table) // 4, pad(table) + data


def chunk(tag, payload):
    payload += b"\0" * (-len(payload) % 16)
    return tag + struct.pack("<I8x", len(payload)) + payload


def name32(s):
    return s.encode("ascii", "replace")[:31].ljust(32, b"\0")


def rot_mtx(rot):
    """R = Rz * Ry * Rx of Euler angles in degrees (the engine's mat_translate_rot_scale), rows."""
    cx, sx, cy, sy, cz, sz = (f(math.radians(a)) for a in (rot[0], rot[1], rot[2]) for f in (math.cos, math.sin))
    return [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
            [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
            [-sy, cy * sx, cy * cx]]


def mtx_euler(r):
    """Inverse of rot_mtx -> degrees."""
    ry = math.asin(max(-1.0, min(1.0, -r[2][0])))
    if abs(r[2][0]) < 0.99999:
        rx, rz = math.atan2(r[2][1], r[2][2]), math.atan2(r[1][0], r[0][0])
    else:  # gimbal lock: put everything in x
        rx, rz = math.atan2(-r[1][2], r[1][1]), 0.0
    return [math.degrees(a) for a in (rx, ry, rz)]


def mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


BEZIER_STEPS = 8  # samples per curved rail part


def bezier_polyline(pts, cin, cout, closed, marks=None):
    """Petari BezierRail: part i runs from pnt0[i] (control pnt2[i]) to pnt0[i+1] (control pnt1[i+1]); a part whose
    controls sit on its ends is straight. -> the rail as points along it (closed rails without the repeated start)."""
    n = len(pts)
    if n < 2:
        return list(pts)
    out = [pts[0]]
    if marks is not None:
        marks.append(0)
    for i in range(n if closed else n - 1):
        a, b, c, d = pts[i], cout[i], cin[(i + 1) % n], pts[(i + 1) % n]
        near = lambda u, v: sum((u[k] - v[k]) ** 2 for k in range(3)) < 1.0
        if near(a, b) and near(c, d):
            out.append(d)
        else:
            for s in range(1, BEZIER_STEPS + 1):
                t = s / BEZIER_STEPS
                w = ((1 - t) ** 3, 3 * t * (1 - t) ** 2, 3 * t * t * (1 - t), t ** 3)
                out.append(tuple(w[0] * a[k] + w[1] * b[k] + w[2] * c[k] + w[3] * d[k] for k in range(3)))
        if marks is not None and (i + 1 < n):
            marks.append(len(out) - 1)
    return out[:-1] if closed else out


def path_entry(p):
    marks = []
    dense = bezier_polyline(p["pts"], p.get("cin", p["pts"]), p.get("cout", p["pts"]), p["closed"], marks)
    along = [0.0]
    for i in range(1, len(dense)):
        along.append(along[-1] + math.dist(dense[i - 1], dense[i]))
    n = len(p["pts"])
    a0, a5, a1, a2 = (p.get(k, [-1] * n) for k in ("arg0", "arg5", "arg1", "arg2"))
    return (struct.pack("<HHiIIii", p["zone"], p["closed"], p["id"], len(dense), n, p.get("stop", -1), 0) +
            b"".join(struct.pack("<3f", *pt) for pt in dense) + b"".join(struct.pack("<3f", *pt) for pt in p["pts"]) +
            b"".join(struct.pack("<fii", along[marks[i]] if i < len(marks) else along[-1], a0[i], a5[i]) for i in range(n)) +
            b"".join(struct.pack("<ii", a1[i], a2[i]) for i in range(n)))


def to_world(zones, up_ids, items):
    """Zone-local rows (pos, rot) -> world, in place: world = zone placement composed up its chain of parents.
    Returns each zone's world (rotation rows, translation)."""
    world = {}

    def zw(z):
        if z not in world:
            r, t = rot_mtx(zones[z]["rot"]), zones[z]["pos"]
            if z in up_ids:
                pr, pt = zw(up_ids[z])
                r, t = mul3(pr, r), [pt[i] + sum(pr[i][k] * t[k] for k in range(3)) for i in range(3)]
            world[z] = (r, list(t))
        return world[z]
    for it in items:
        z = it["zone"]
        if z == 0 or (zones[z]["pos"] == [0, 0, 0] and zones[z]["rot"] == [0, 0, 0] and z not in up_ids):
            continue
        r, t = zw(z)
        if "pts" in it:
            for key in ("pts", "cin", "cout"):
                it[key] = [tuple(t[i] + sum(r[i][k] * pt[k] for k in range(3)) for i in range(3)) for pt in it.get(key, [])]
            continue
        it["pos"] = [t[i] + sum(r[i][k] * it["pos"][k] for k in range(3)) for i in range(3)]
        it["rot"] = mtx_euler(mul3(r, rot_mtx(it["rot"])))
    return [zw(z) for z in range(len(zones))]


GRAV_KINDS = {"GlobalPointGravity": 0, "GlobalPlaneGravity": 1, "GlobalPlaneGravityInBox": 2,
              "GlobalPlaneGravityInCylinder": 3, "GlobalCubeGravity": 4, "GlobalDiskGravity": 5,
              "GlobalSegmentGravity": 6, "GlobalDiskTorusGravity": 7, "GlobalConeGravity": 8, "GlobalWireGravity": 9}


def rotate_about(v, axis, deg):
    """Rodrigues: v rotated by deg around unit axis (TRot3f::setRotate + mult)."""
    a = math.radians(deg)
    c, sn = math.cos(a), math.sin(a)
    cr = [axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]]
    d = sum(axis[i] * v[i] for i in range(3))
    return [v[i] * c + cr[i] * sn + axis[i] * d * (1 - c) for i in range(3)]


def unit(v):
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v] if n > 1e-9 else [0.0, 0.0, 0.0]


def grav_record(o, paths=(), wire=None, row=0):
    """One gravity row -> GRAV bytes, or None for kinds the engine has no evaluator for (Petari GravityCreator.cpp:
    settingFromSRT / settingFromJMapArgs, the world matrix baked in). m = x, y, z axes + translation; p, flags:
    plane (1-3): p0 base distance; box: m = range box (axes scaled to the half extents), flags = distance calc type
    (0 X, 1 Y, 2 Z, 3 default); cylinder: p1 radius, p2 height. cube (4): m = cube (axes = half extents), flags =
    active faces. disk (5): m.t centre, m.y normal, m.x the valid-sector direction, p0 radius, p1 valid cos, flags
    bit 0 both sides, bit 1 edge gravity. segment (6): m.t point 0, m.y unit axis, m.x valid-side direction, p0 axis
    length, p1 valid cos, flags bit 0/1 = edge 0/1 valid. torus (7): m.t centre, m.y unit axis, p0 radius, p1 disk
    radius, flags bit 0 both sides, bits 1-2 edge type. cone (8): m = TRS axes x 500 scale (y up to the apex), p0 top
    cut rate, flags bit 0 bottom on. wire (9): p0 its first point in `wire` (the points after the records), p1 how
    many: WireGravityCreator::settingFromJMapOtherParam, arg0 + 2 points (arg0 20 if -1) spaced evenly along its rail,
    the last (the rail's end) left out as Petari does."""
    kind = GRAV_KINDS.get(o["name"])
    if kind is None or o.get("gtype") in ("Shadow", "Magnet"):  # Shadow / Magnet: not what Mario falls by
        return None
    r, t, sc = rot_mtx(o["rot"]), list(o["pos"]), o["scale"]
    ax = [[r[i][k] for i in range(3)] for k in range(3)]  # unit axes x, y, z
    a = [x if -2**31 <= x < 2**31 else -1 for x in o["args"]]
    m, pr, flags = ax + [t], [0.0] * 4, 0
    distant = o["distant"]  # getJMapInfoArgPlus: a negative Distant / Range / Priority keeps the default
    if distant is None or distant < 0:
        distant = 500.0 * sc[0] if kind == 0 else 0.0
    base = 2000.0 if a[0] < 0 else float(a[0])
    if kind == 1:
        pr[0] = 2000.0
    elif kind in (2, 4):  # box range / cube: TR, translation up by 500 sy, axes scaled by 500 s
        h = [500.0 * x for x in sc]
        m = [[c * h[k] for c in ax[k]] for k in range(3)] + [[t[i] + ax[1][i] * h[1] for i in range(3)]]
        if kind == 2:
            pr[0], flags = base, a[1] if a[1] in (0, 1, 2) else 3
        else:
            flags = (a[0] & 1) | (a[0] & 2) | (a[1] & 1) << 2 | (a[1] & 2) << 2 | (a[2] & 1) << 4 | (a[2] & 2) << 4
    elif kind == 3:
        pr[:3] = [base, 500.0 * sc[0], 500.0 * sc[1]]
    elif kind == 5:
        deg = float(a[2]) if a[2] >= 0 else 360.0
        side = unit([ax[0][i] - ax[1][i] * sum(ax[0][j] * ax[1][j] for j in range(3)) for i in range(3)])
        m[0] = rotate_about(side, ax[1], 0.5 * deg)
        pr[:2] = [500.0 * max(sc), math.cos(math.radians(0.5 * deg))]
        flags = (a[0] != 0) | (a[1] != 0) << 1
    elif kind == 7:
        pr[:2] = [500.0 * max(sc), max(float(a[2]), 0.0)]
        flags = (a[0] != 0) | (a[1] if a[1] in (0, 1, 2) else 3) << 1
    elif kind == 8:
        m = [[c * 500.0 * sc[k] for c in ax[k]] for k in range(3)] + [t]
        pr[0], flags = min(max(a[1] / 1000.0, 0.0), 1.0), int(a[0] != 0)
    elif kind == 9:
        rail = next((q for q in paths if q["zone"] == o["zone"] and q["id"] == o["path"]), None)
        if rail is None:
            return None
        dense = bezier_polyline(rail["pts"], rail.get("cin", rail["pts"]), rail.get("cout", rail["pts"]), rail["closed"])
        if rail["closed"]:
            dense = dense + [dense[0]]
        along = [0.0]
        for i in range(1, len(dense)):
            along.append(along[-1] + math.dist(dense[i - 1], dense[i]))
        n = (a[0] if a[0] != -1 else 20) + 2
        pr[:2] = [len(wire) // 3, n - 1]
        for i in range(n - 1):
            c, j = i * along[-1] / (n - 1), 1
            while j < len(along) - 1 and along[j] < c:
                j += 1
            f = (c - along[j - 1]) / max(along[j] - along[j - 1], 1e-9)
            wire += [dense[j - 1][k] + (dense[j][k] - dense[j - 1][k]) * f for k in range(3)]
    elif kind == 6:  # TRS: the axis is 1000 * scale.y long, the side vector the (unit) X axis
        deg = float(a[1]) if a[1] >= 0 else 360.0
        side = unit([ax[0][i] - ax[1][i] * sum(ax[0][j] * ax[1][j] for j in range(3)) for i in range(3)])
        m[0] = rotate_about(side, ax[1], 0.5 * deg)
        pr[:2] = [1000.0 * abs(sc[1]), math.cos(math.radians(0.5 * deg))]
        flags = {0: 0, 1: 1, 2: 2}.get(a[0], 3)
    return struct.pack("<HHiiff12f4fi", kind, flags, max(o["priority"], 0), 1 if o["inverse"] == 1 else 0, o["range"],
                       distant, *[x for v in m for x in v], *pr, row)


_factory = None


def model_archive(fsp, name, planets):
    """Archive holding a placed class's model: the planet itself, else the first archive Petari's NameObjFactory gives
    the name (cCreateTable / cName2ArchiveNamesTable, budget.factory_tables), else the name; None if not on disc."""
    global _factory
    if name in planets or name in OBJ_TYPES or name in SKINNED:
        return name
    if name in NO_DRAW:
        return None
    if name in MODEL_ARC:
        return MODEL_ARC[name]
    if name.startswith("Mini") and name.endswith("Galaxy") and (fsp / "ObjectData" / f"{name}.arc").exists():
        return name  # MiniatureGalaxy: its own model (the factory lists the unknown one first); MINI_EXTRA the rest
    if _factory is None:  # parsing Petari's sources takes minutes on the SD card: cached next to the other outputs
        cache = Path(__file__).parent.parent / "out" / "factory.json"
        if cache.exists():
            _factory = json.loads(cache.read_text())
        else:
            _factory = B.factory_tables()[1]
            cache.write_text(json.dumps(_factory))
    for a in _factory.get(name, []) + [name]:
        if (fsp / "ObjectData" / f"{a}.arc").exists():
            return a
    return None


def s32(v):
    return v - 2**32 if v >= 2**31 else v


def declare_stars(keep, scenario, appear, jp):
    """Points each event star's arg7 at its declarer (STAR_DECLARERS). appear: {scenarioNo: AppearPowerStarObj},
    jp: ObjNameTable {class: Japanese name}."""
    def find(name):  # PowerStarEventKeeper::findStarID: the current scenario first
        if appear.get(scenario) == name:
            return scenario
        return next((n for n, a in sorted(appear.items()) if a == name), -1)
    marks = []
    for i, r in enumerate(keep):
        k = STAR_DECLARERS.get(r["name"], -1)
        if r["name"] == "NoteFairy":  # declarePowerStar when arg7 is 1-3
            k = None if s32(r["args"][7]) > 0 else -1
        elif r["name"] in ("CollectTico", "DinoPackun", "OtaKing"):  # their init declarePowerStar, always
            k = None
        elif k is None and s32(r["args"][7]) != 7:
            k = -1
        if k == -1:
            continue
        sid = s32(r["args"][k]) if k is not None else -1
        if r["name"] == "FlipPanelObserver":
            sid = -1 if sid < 0 else sid or find(jp.get(r["name"]))
        elif sid < 0:
            sid = find(jp.get(r["name"]))
        marks += [(sid, i)] if sid > 0 else []
    if appear.get(scenario) == COIN100:
        marks.append((scenario, -2))
    for sid, i in marks:
        for r in keep:
            if r["name"] == "PowerStar" and s32(r["args"][0]) == sid:
                r["args"] = r["args"][:7] + [i]


def placement(fs, galaxy, scenario):
    """Scenario-wide chunks (TYPE ZONE OBJS STRT PATH GRAV CAMS CARE) and what the asset packs need."""
    act, zones, objs, starts, paths, cubes, bcams, up, demos, gpos = scene(fs, galaxy, scenario)
    # everything placed in a zone goes to world space here, so the engine never composes zone matrices.
    # ponytail: CAMS vectors (fixed eye points, tower axes) stay zone-local; fine while zones sit at the origin
    # unrotated, transform them in camera.c by the CAMS zone when a galaxy needs it
    zid = {z["name"]: i for i, z in enumerate(zones)}
    up_ids = {zid[z]: zid[p] for z, p in up.items() if z in zid and p in zid and zid[p] != 0}
    zworld = to_world(zones, up_ids, objs + starts + paths + cubes + gpos + [o["home"] for o in objs if "home" in o])
    for o in objs:  # the Astro classes drawn with their dome's model (ASTRO_MODELS)
        if o["name"] in ASTRO_MODELS and 1 <= o["args"][0] <= 6:
            o["name"] = ASTRO_MODELS[o["name"]][o["args"][0] - 1]
    for o in objs:  # POSE classes are LiveActors with a BCK, not MapPartsRotators: their mappartsinfo columns unused
        if o["name"] in POSE or o["name"] in KCL_PARTS:
            o["args"][4:8] = [-1, -1, -1, -1]
    for o in objs:  # AssemblyBlock: its home point (world) in args 2-6 (assembly_home in game.c)
        if o["name"].startswith("AssemblyBlock"):
            h = o.get("home") or dict(pos=[o["pos"][0], o["pos"][1] + 1000.0, o["pos"][2] + 1000.0], rot=[0.0, 0.0, 0.0])
            rx, ry, rz = (int(round(a)) % 360 for a in h["rot"])
            o["args"][2:7] = [int(round(x)) for x in h["pos"]] + [rx << 16 | ry, rz]
    fsp = Path(fs)
    planets = B.planet_table(fsp, lambda a: (fsp / "ObjectData" / f"{a}.arc").exists())
    # every class with a model on disc is kept (static until it gets a behaviour), plus gravities
    arcmap = {n: model_archive(fsp, n, planets) for n in {o["name"] for o in objs}}
    keep = [o for o in objs if arcmap[o["name"]] or "Gravity" in o["name"] or o["name"] in LOGIC or o["name"] in jpa.AMBIENT or o["name"] in ("WaterPlant", "PowerStarAppearPoint", "CollectTico", "OceanBowl", "ScenarioStarter", "StarPieceFollowGroup", "CoinReplica", "PoleNoModel", "PoleSquareNoModel", "WhirlPoolAccelerator", "ArrowSwitchTarget", "LightCtrlCube", "LightCtrlCylinder", "LensFlareArea") or o["name"].startswith(("FishGroup", "SeaGullGroup", "BloomCube", "BloomSphere", "BloomCylinder", "SimpleBloom", "ScreenBlur", "DepthOfField"))]
    for o in keep:  # MiniatureGalaxy::initPartsModel: its unknown / hatena / select ring / star number parts
        if o["name"].startswith("Mini") and o["name"].endswith("Galaxy"):
            EXTRA.setdefault(o["name"], MINI_EXTRA)
    for o in keep:
        for e in EXTRA.get(o["name"], ()):
            if e[0] == "#":
                arcmap.setdefault(o["name"] + e, arcmap[o["name"]] + e if arcmap[o["name"]] else None)
            else:
                arcmap.setdefault(e, e if (fsp / "ObjectData" / f"{e.partition('#')[0]}.arc").exists() else None)
    sd = {n.lower(): d for n, d in B.rarc_files((fsp / "StageData" / galaxy / f"{galaxy}Scenario.arc").read_bytes())}
    sd = sd["scenariodata.bcsv"]
    names = next(B.rarc_files((fsp / "StageData" / "ObjNameTable.arc").read_bytes()))[1]
    try:
        comet = B.bcsv_column(sd, "Comet")
    except Exception:
        comet = []
    # GalaxyStatusAccessor::getAppearPowerStarObjName: a Purple / Black comet scenario's star is the 100-coin one
    appear = {n: COIN100 if i < len(comet) and comet[i] in ("Purple", "Black") else a
              for i, (n, a) in enumerate(zip(B.bcsv_column(sd, "ScenarioNo"), B.bcsv_column(sd, "AppearPowerStarObj")))}
    declare_stars(keep, scenario, appear,
                  dict(zip(B.bcsv_column(names, "en_name"), B.bcsv_column(names, "jp_name"))))
    types = sorted({o["name"] for o in keep} | {s["name"] for s in starts})
    tid = {t: i for i, t in enumerate(types)}
    pack_row = lambda r: ROW.pack(tid[r["name"]], r["zone"], r["l_id"], r["path"], *r["pos"], *r["rot"], *r["scale"],
                                  *[a if -2**31 <= a < 2**31 else -1 for a in r["args"]],
                                  *[v if -2**15 <= v < 2**15 else -1 for v in r.get("sw", [-1] * 5)],
                                  r.get("group", -1) if -2**15 <= r.get("group", -1) < 2**15 else -1)
    out = [chunk(b"TYPE", struct.pack("<I", len(types)) + b"".join(name32(t) for t in types)),
           chunk(b"ZONE", struct.pack("<I", len(zones)) + b"".join(
               name32(z["name"]) + struct.pack("<6f", *t, *mtx_euler(r)) for z, (r, t) in zip(zones, zworld))),
           chunk(b"OBJS", struct.pack("<I", len(keep)) + b"".join(pack_row(r) for r in keep)),
           chunk(b"STRT", struct.pack("<I", len(starts)) + b"".join(pack_row(r) for r in starts)),
           chunk(b"PATH", struct.pack("<I", len(paths)) + b"".join(path_entry(p) for p in paths))]
    AUTO_SKINNED.update({t: arcmap[t] for t in types if t not in planets and t not in SKINNED_OBJ and arcmap.get(t)
                         and "#" not in arcmap[t] and auto_anim(fs, arcmap[t])})
    out.append(obj_anim_chunk(Path(fs), set(types), galaxy))
    wire = []
    grav = [g for g in (grav_record(o, paths, wire, i) for i, o in enumerate(keep)) if g]
    out.append(chunk(b"GRAV", struct.pack("<I", len(grav)) + b"".join(grav) + struct.pack(f"<{len(wire)}f", *wire)))
    out.append(chunk(b"ZPAR", struct.pack(f"<I{len(zones)}i", len(zones),
                                          *[-1 if i == 0 else up_ids.get(i, 0) for i in range(len(zones))])))
    jp2en = dict(zip(B.bcsv_column(names, "jp_name"), B.bcsv_column(names, "en_name")))
    cams, cam_index, cam_types = camera.cameras(bcams, float_column, int_or, jp2en)
    cset = [(i, r["camset"]) for i, r in enumerate(keep) if r.get("camset", -1) >= 0]  # ActorCameraInfo: CameraSetId
    out.append(chunk(b"CSET", struct.pack("<I", len(cset)) + b"".join(struct.pack("<Hh", i, v) for i, v in cset)))
    out.append(demo_chunk(fsp, demos, gpos, keep, jp2en, se_table()))
    out.append(canm_chunk(Path(fs), set(types), galaxy, scenario))  # after demo_chunk: DEMO_CANM
    care = [struct.pack("<BBH9f", camera.SHAPES[r["name"]], r["zone"],
                        cam_index.get((r["zone"], "c", r["args"][0] & 0xFFFF), 0xFFFF), *r["pos"], *r["rot"], *r["scale"])
            for r in camera.areas(cubes)]
    out.append(chunk(b"CAMS", struct.pack("<I12x", len(cams)) + b"".join(cams)))
    out.append(chunk(b"CARE", struct.pack("<I12x", len(care)) + b"".join(care)))
    # asset names per stage, in draw order: planets first, then object types
    names = [z["name"] for z in zones]
    per_stage = {st: [] for st in act}
    for o in keep:
        n = o["name"]
        if n in BRK_ARG and arcmap.get(n):  # its colour's model instead (BRK_ARG)
            v = o["args"][BRK_ARG[n][1]]
            n = f"{n}~{v if v >= 0 else 0}"
            arcmap.setdefault(n, f"{arcmap[o['name']]}~{BRK_ARG[o['name']][0]}:{n.partition('~')[2]}")
        bn = n + "Bloom"  # PlanetMap::initBloomModel / a class's bloom model: "<name>Bloom" drawn in the bloom pass
        if arcmap.get(n) and (fsp / "ObjectData" / f"{bn}.arc").exists():
            arcmap.setdefault(bn, bn)
            if bn not in per_stage[names[o["zone"]]]:
                per_stage[names[o["zone"]]].append(bn)
        if n in BRK_STEPS and arcmap.get(n):  # its BRK frames' models (BRK_STEPS)
            for k, fr in enumerate(BRK_STEPS[n][1]):
                arcmap.setdefault(f"{n}~{k}", f"{arcmap[n]}~{BRK_STEPS[n][0]}:{fr}")
                if f"{n}~{k}" not in per_stage[names[o["zone"]]]:
                    per_stage[names[o["zone"]]].append(f"{n}~{k}")
        for m in (n,) + tuple(n + e if e[0] == "#" else e for e in EXTRA.get(o["name"], ())):
            if arcmap[m] and m not in per_stage[names[o["zone"]]]:
                per_stage[names[o["zone"]]].append(m)
    for st in per_stage:
        per_stage[st].sort(key=lambda n: (n not in planets, n))
    info = dict(stages=act, types=types, objs=len(keep), starts=len(starts), paths=len(paths), cameras=len(cams),
                areas=len(care), camera_types=dict(cam_types))
    # TALK: the messages of the rows with a MessageId (TalkNodeCtrl::createFlowNode: <zone>_<class><id %03d>) and
    # the classes' direct Common_ ones
    ctls = [(f"{zones[r['zone']]['name']}_{r['name']}{r['mid']:03d}", i) for i, r in enumerate(keep) if r.get("mid", -1) >= 0]
    ctls += [(lab, None) for c in sorted(msg.COMMON) if c in types for lab in msg.COMMON[c]]
    ctls += [(lab, i) for i, r in enumerate(keep) for lab in msg.DIRECT.get(r["name"], ())]
    ctls += [(lab, None) for lab in msg.SYSTEM]
    ctls += [(f"GalaxyName_{galaxy}", None), (f"ScenarioName_{galaxy}{scenario}", None)]  # ScenarioTitle (StageTitle)
    out.append(chunk(b"TALK", msg.talk_chunk(fsp, ctls)))
    out.append(chunk(b"LGHT", light.chunk_payload(fsp, [z["name"] for z in zones])[0]))  # tools/light.py
    # TLGT: per TYPE its actor light type (tools/lighttype.py: 0 Player, 1 Strong, 2 Weak, 3 Planet, 255 unknown)
    out.append(chunk(b"TLGT", bytes(LIGHT_TYPES.get(t, 255) for t in types)))
    return out, info, dict(planets=planets, per_stage=per_stage, up=up, arcmap=arcmap)


_model_cache = {}


def assets(fsp, models, planets, ratio=0.99, stats=None, collision=True, fx=False):
    """Asset chunks (TEXT MODL SKIN VUMD KCL BTK) for [(model name, archive name)]: textures deduplicated within the pack.
    KCL for planets when `collision`. Converted models are cached per process (one archive, many scenarios). fx: also
    the particle effects (PTCL, tools/jpa.py pack), their textures first in TEXT."""
    stats = stats if stats is not None else {}
    models_, kcls, vumd, skins, btks, swaps = [], [], [], [], [], []
    texts, tex_id, ptcl = [], {}, []
    if fx:
        payload, texts = jpa.pack(fsp)
        fpay, ftex, _ = msg.font_chunk(fsp)  # the talk font's atlases right after the effects' textures
        ft0 = len(texts)
        texts = texts + ftex
        fpay = fpay[:4] + struct.pack("<HH", ft0, ft0 + 1) + fpay[8:]
        ptcl = [chunk(b"PTCL", payload), chunk(b"FONT", fpay)]
    for t, a in models:
        a, _, pose = a.partition("#")  # "<archive>#<BCK>": the model with that animation's last-frame visibility
        a, _, bv = a.partition("~")  # "<archive>~<BRK>:<frame>": baked at that BRK frame (BRK_ARG)
        brk = (bv.split(":")[0], int(bv.split(":")[1])) if bv else None
        a, _, mk = a.partition("@")  # "<archive>@<k>": material colour k (MAT_COLOR)
        mc = (MAT_COLOR[a][0], tuple(MAT_COLOR[a][1][int(mk)])) if mk else None
        cls = t.partition("~")[0]
        arc = fsp / "ObjectData" / f"{a}.arc"
        if not arc.exists():
            continue
        key = (a, pose, ratio, brk, mc)
        if key not in _model_cache:
            st = {}
            _model_cache[key] = (model_groups(arc, ratio, st, cls in SKINNED or cls in SKINNED_OBJ or cls in AUTO_SKINNED,
                                              pose or None, brk, mc), st)
        (groups, kcl, sk), st = _model_cache[key]
        for k, v in st.items():
            if not k.startswith("_"):
                stats[k] = stats.get(k, 0) + v
        btks += [name32(t) + e for e in st.get("_btk", [])]
        if sk:
            skins.append(name32(t) + sk)
        # MODL keeps only the bounding radius (culling): drawing uses VUMD, collision KCL
        models_.append(name32(t) + struct.pack("<f", math.sqrt(max((sum(c * c for c in v) for verts, *_ in groups
                                                                    for v in verts), default=0.0))))
        vg, qs = b"", pos_scale(groups)
        for gi, ((verts, tris, uvs, tx, _, _, bl, _, _), (pos, col, uv, nb)) in enumerate(zip(groups, vu_groups(groups, BASE_COLOUR.get(t, (112, 176, 96)),
                                                                             stats, qs))):
            ti = 0xFFFF
            if tx:
                key = tx["pix"] + tx["clut"] + bytes((tx["clamp_s"], tx["clamp_t"]))
                ti = tex_id.setdefault(key, len(texts))
                if ti == len(texts):
                    texts.append(dict(tx, lod=[0.0, 0.0]))
                texts[ti]["lod"] = [a + b for a, b in zip(texts[ti]["lod"], lod_sums(verts, tris, uvs, tx))]
                ids = []
                for a_ in tx.get("alts", []):  # SWAP: {char model[32], u16 group, u16 n, u16 texture[n] (scene-relative), pad}
                    k2 = a_["pix"] + a_["clut"] + bytes((a_["clamp_s"], a_["clamp_t"]))
                    t2 = tex_id.setdefault(k2, len(texts))
                    if t2 == len(texts):
                        texts.append(dict(a_, lod=list(texts[ti]["lod"])))
                    ids.append(t2)
                if ids:
                    sw = struct.pack("<HH", gi, len(ids) + 1) + struct.pack(f"<{len(ids) + 1}H", ti, *ids)
                    swaps.append(name32(t) + sw + b"\0" * (-len(sw) % 8))
            nbt, body = index_batches(pos, col, uv, len(tris), nb)
            env = tx.get("env") if tx else None  # lit byte 2: VU1Env, its 12-float matrix (tex.env_mtx) after the header
            vg += struct.pack("<IHHBBBB4x", len(tris), ti, nbt, *bl, 2 if env else 1 if nb else 0) + \
                (struct.pack("<12f", *env) if env else b"") + body
        vumd.append(name32(t) + struct.pack("<If8x", len(groups), qs) + vg)
        if kcl and collision:
            kcl = kcl_to_le(kcl)
            kcls.append(name32(t) + struct.pack("<I", len(kcl)) + kcl + b"\0" * (-len(kcl) % 4))
        if collision and cls in KCL_PARTS:  # its joint collisions (KCL_PARTS)
            fl = {k.lower().rsplit("/", 1)[-1]: v for k, v in B.rarc_files(arc.read_bytes())}
            for kn, jn in KCL_PARTS[cls].items():
                if kn in fl:
                    pk = kcl_to_le(kcl_codes(fl[kn], fl.get(kn[:-4] + ".pa", b"")))
                    kcls.append(name32(f"{t}#{jn}") + struct.pack("<I", len(pk)) + pk + b"\0" * (-len(pk) % 4))
    head, data = bytearray(), bytearray()
    base = 16 + 32 * len(texts)
    for tx in texts:
        pix = base + len(data)
        for lv in [tx["pix"]] + tx.get("mips", []):  # base level then the mips, each padded to 16
            data += lv + b"\0" * (-len(lv) % 16)
        size = base + len(data) - pix
        clut = base + len(data)
        data += tx["clut"]
        head += struct.pack("<HHBBBBIIIBBBxh6x", tx["w"], tx["h"], tx["psm"], tx["clamp_s"], tx["clamp_t"], tx["alpha"],
                            pix, size, clut, tx["w"].bit_length() - 1, tx["h"].bit_length() - 1,
                            len(tx.get("mips", [])), lod_k(tx.get("lod", (0, 0))))
    stats.update(models=stats.get("models", 0) + len(models_), kcls=stats.get("kcls", 0) + len(kcls),
                 textures=stats.get("textures", 0) + len(texts))
    return [chunk(b"TEXT", struct.pack("<I12x", len(texts)) + bytes(head) + bytes(data)),
            chunk(b"MODL", struct.pack("<I", len(models_)) + b"".join(models_)),
            chunk(b"SKIN", struct.pack("<I", len(skins)) + b"".join(skins)),
            chunk(b"VUMD", struct.pack("<I12x", len(vumd)) + b"".join(vumd)),
            chunk(b"KCL ", struct.pack("<I", len(kcls)) + b"".join(kcls)),
            chunk(b"BTK ", struct.pack("<I", len(btks)) + b"".join(btks)),
            chunk(b"SWAP", struct.pack("<I", len(swaps)) + b"".join(swaps))] + ptcl


def anim_chunk(fsp):
    clips = []
    for n, b in B.rarc_files((fsp / "ObjectData" / "MarioAnime.arc").read_bytes()):
        if n.lower().endswith(".bck"):
            raw = anim_clip(b)
            z = zlib.compress(raw, 9)
            clips.append(name32(n.rsplit("/", 1)[-1][:-4].lower()) + struct.pack("<II", len(raw), len(z)) + z +
                         b"\0" * (-len(z) % 4))
    return chunk(b"ANIM", struct.pack("<I", len(clips)) + b"".join(clips))


def eye_chunk(fsp):
    """EYEP: Mario's eye patterns (MarioActor::setBlink: the BTP of a clip's name, J3D1btp1 / TPT1), the EyeLid_v
    material's texture each frame as 0..3 over mario_eyeLid.0: {u32 n}, n x {char clip[32], u16 frames, u8 loop
    (2 = repeat: its own frame count; else the clip's frame), u8 pad, u8 eyelid[frames], padded to 4}."""
    import tex
    mb = next(v for k, v in B.rarc_files((fsp / "ObjectData" / "Mario.arc").read_bytes()) if k.lower().endswith(".bdl"))
    bl = tex.blocks(mb)
    tnames = tex.names(mb, bl[b"TEX1"] + tex.u32(mb, bl[b"TEX1"] + 0x10))
    base = tnames.index("mario_eyeLid.0")
    out = []
    for n, b in B.rarc_files((fsp / "ObjectData" / "MarioAnime.arc").read_bytes()):
        if not n.lower().endswith(".btp") or b[:8] != b"J3D1btp1":
            continue
        t = b.find(b"TPT1")
        loop, fmax, nmat = b[t + 8], struct.unpack_from(">H", b, t + 0xA)[0], struct.unpack_from(">H", b, t + 0xC)[0]
        mt_off, ix_off, nm_off = (struct.unpack_from(">I", b, t + k)[0] for k in (0x10, 0x14, 0x1C))
        mats = tex.names(b, t + nm_off)
        if "EyeLid_v" not in mats:
            continue
        m = mats.index("EyeLid_v")
        cnt, first = struct.unpack_from(">HH", b, t + mt_off + 8 * m)
        keys = [struct.unpack_from(">H", b, t + ix_off + 2 * (first + k))[0] for k in range(cnt)]
        frames = max(fmax, 1)
        eye = bytes(min(max(keys[min(f, cnt - 1)] - base, 0), 3) if cnt else 0 for f in range(frames))
        out.append(name32(n.rsplit("/", 1)[-1][:-4].lower()) + struct.pack("<HBB", frames, loop, 0) + eye +
                   b"\0" * (-len(eye) % 4))
    return chunk(b"EYEP", struct.pack("<I", len(out)) + b"".join(out))


# clips a class never plays in a stage: Butler::init's initForAstroGalaxy registers none of its talk anim demos (their
# DemoButler* clips are the dome's), and DemoWithButler is ButlerExplain's (グランドスター１帰還)
def anim_skipped(cls, clip, galaxy):
    return cls == "Butler" and (clip == "demowithbutler" or (clip.startswith("demobutler") and galaxy != "AstroDome"))


def obj_anim_chunk(fsp, classes, galaxy=None):
    """ANIM for the skinned placed classes among `classes` (and their EXTRA models): "<class>/<clip>" lower case."""
    clips = []
    names = [c for c in SKINNED_OBJ if c in classes or any(c in EXTRA.get(k, ()) for k in classes)]
    names += [c for c in AUTO_SKINNED if c in classes and c not in names]
    for c in names:
        arc = fsp / "ObjectData" / f"{AUTO_SKINNED.get(c) or ANIM_ARC.get(c, c)}.arc"
        if not arc.exists():
            continue
        files = {n.rsplit("/", 1)[-1].lower(): b for n, b in B.rarc_files(arc.read_bytes())}
        packed = {}
        for n, b in files.items():
            if n.endswith(".bck") and not anim_skipped(c, n[:-4], galaxy):
                raw = anim_clip(b)
                z = zlib.compress(raw, 9)
                packed[n[:-4]] = struct.pack("<II", len(raw), len(z)) + z + b"\0" * (-len(z) % 4)
                clips.append(name32(f"{c.lower()}/{n[:-4]}") + packed[n[:-4]])
        # ActorAnimKeeper (actoranimctrl.bcsv): an action whose BckName is another clip plays that one (startAction)
        ctl = files.get("actoranimctrl.bcsv", b"")
        for act, bck in zip(B.bcsv_column(ctl, "ActorAnimName"), B.bcsv_column(ctl, "BckName")):
            a, k = act.lower(), (bck or "").lower()
            if k and k != a and k in packed and a not in packed and a.isascii():
                clips.append(name32(f"{c.lower()}/{a}") + packed[k])
    return chunk(b"ANIM", struct.pack("<I", len(clips)) + b"".join(clips))


def canm_bake(b):
    """A camera animation (Petari CameraAnim.cpp: ANDO CANM linear per frame, CKAN Hermite keys with tangents / 30)
    as one (pos xyz, watch xyz, twist, fovy) per frame, in its target actor's frame."""
    if b[:4] != b"ANDO" or b[4:8] not in (b"CANM", b"CKAN") or struct.unpack(">i", b[8:12])[0] == 0:
        return []
    key = b[4:8] == b"CKAN"
    frames, voff = struct.unpack(">II", b[0x18:0x20])
    entry = b[0x20:]
    nval = struct.unpack(">I", entry[voff:voff + 4])[0] // 4
    vals = struct.unpack(f">{nval}f", entry[voff + 4:voff + 4 + 4 * nval])
    comps = [struct.unpack(">III" if key else ">II", entry[i * (12 if key else 8):(i + 1) * (12 if key else 8)]) for i in range(8)]

    def herm(f, t0, v0, m0, t1, v1, m1):
        d = t1 - t0
        t = (f - t0) / d if d else 0.0
        t2 = t * t
        return v0 + t * d * m0 + t2 * (3 * (v1 - v0) - 2 * d * m0 - d * m1) + t2 * t * (2 * (v0 - v1) + d * m0 + d * m1)

    def get(f, c):
        count, off = c[0], c[1]
        if not key:
            k = int(f)
            r = min(max(f - k, 0.0), 1.0)
            if k < count - 1:
                return (1 - r) * vals[off + k] + r * vals[off + k + 1]
            return vals[off + count - 1]
        if count == 1:
            return vals[off]
        st = 3 if c[2] == 0 else 4
        ks = [vals[off + i * st:off + i * st + st] for i in range(count)]
        if f <= ks[0][0]:
            return ks[0][1]
        if f >= ks[-1][0]:
            return ks[-1][1]
        i = max(j for j in range(count) if ks[j][0] <= f)
        a, z = ks[i], ks[i + 1]
        return herm(f, a[0], a[1], (a[2] if st == 3 else a[3]) / 30.0, z[0], z[1], z[2] / 30.0)
    return [tuple(get(float(f), c) for c in comps) for f in range(frames)]


def canm_chunk(fsp, classes, galaxy=None, scenario=None):
    """CANM: the skinned placed classes' camera animations, "<class>/<name>" lower case, baked per frame; and the
    scenario's opening camera (CameraDirector::createStartAnimCamera: StartScenario<n>.canm of the stage archive) as
    "stage/startscenario"."""
    out = []
    sa = fsp / "StageData" / f"{galaxy}.arc"
    if galaxy and sa.exists():
        for n, b in B.rarc_files(sa.read_bytes()):
            if n.lower().rsplit("/", 1)[-1] == f"startscenario{scenario}.canm":
                fr = canm_bake(b)
                if fr:
                    out.append(name32("stage/startscenario") + struct.pack("<I", len(fr)) +
                               b"".join(struct.pack("<8f", *f) for f in fr))
    for c in [c for c in SKINNED_OBJ if c in classes] + (["PowerStar"] if "PowerStar" in classes else []) + \
            sorted({c for c, _ in DEMO_CANM if c in classes and c not in SKINNED_OBJ and c != "PowerStar"}):
        arc = fsp / "ObjectData" / f"{AUTO_SKINNED.get(c) or ANIM_ARC.get(c, c)}.arc"
        if not arc.exists():
            continue
        for n, b in B.rarc_files(arc.read_bytes()):
            if n.lower().endswith(".canm"):
                fr = canm_bake(b)
                if fr:
                    out.append(name32(f"{c.lower()}/{n.rsplit('/', 1)[-1][:-5].lower()}") + struct.pack("<I", len(fr)) +
                               b"".join(struct.pack("<8f", *f) for f in fr))
    return chunk(b"CANM", struct.pack("<I", len(out)) + b"".join(out))


# DemoExecutor's sheets (ObjectData/DemoSheet.arc "Demo<sheet><Time|SubPart|Player|Camera|Action|Wipe|Sound>.bcsv")
WIPES = {"フェードワイプ": 0, "白フェードワイプ": 1, "円ワイプ": 2}  # SceneWipeHolder's wipes by name
DEMO_SHEETS = ("time", "subpart", "player", "camera", "action", "wipe", "sound")
# MR::registerDemoCast(actor, DemoName): casts by the demo's name, not its DemoGroupId (Butler::initForAstroDome /
# initForAstroGalaxy: initDemoSheetTalkAnim registers too)
DEMO_DIRECT_CAST = {"Butler": ("ドームレクチャー１", "ドームレクチャー２", "バトラー報告", "スターピース解説前半", "スターピース解説後半",
                               "バトラーグリーンドライバ説明"),
                    # StarReturnDemoStarter::tryRegisterDemo
                    "StarReturnDemoStarter": ("グランドスター帰還[２回目以降]", "パワースター帰還", "天文ドームスター帰還"),
                    "ButlerMap": ("バトラー報告",)}


_demo_dead = None


def demo_dead_classes():
    """The placed classes that wait dead when a demo makes them appear: MapObjActor's (MapObjActor::initialize:
    isRegisteredDemoActionAppear -> makeActorDead) and those that do so in their own init, from vendor/Petari (the
    factory's names whose class derives from one of them)."""
    global _demo_dead
    if _demo_dead is None:
        pet = Path(__file__).resolve().parent.parent / "vendor" / "Petari"
        parent = {}
        for h in (pet / "include").rglob("*.hpp"):
            for m in re.finditer(r"class\s+(\w+)\s*:\s*public\s+(\w+)", h.read_text(errors="ignore")):
                parent[m.group(1)] = m.group(2)
        bases = {"MapObjActor", "BegomanBaby", "BegomanSpike", "BegomanSpring", "PlanetMap", "SuperSpinDriver",
                 "BlueStarCupsulePlanet", "Tico", "TicoDomeLecture", "GCaptureTarget", "NokonokoLand", "Flag", "OceanSphere", "Pole"}

        def dead(c):
            for _ in range(32):
                if c in bases:
                    return True
                if c not in parent:
                    return False
                c = parent[c]
            return False
        fac = (pet / "src" / "Game" / "NameObj" / "NameObjFactory.cpp").read_text(errors="ignore")
        _demo_dead = {n for n, c in re.findall(r'\{\s*"(\w+)",\s*createNameObj<\s*(\w+)\s*>', fac) if dead(c)}
    return _demo_dead


def demo_chunk(fsp, demos, gpos, keep, jp2en, se_ids):
    """DEMO: the scenario's DemoGroups (DemoExecutor), their sheets with every part name resolved to an index (main
    parts 0.., sub parts 0x80 | j, rows naming no part left out), their casts (the OBJS rows whose DemoGroupId is the
    group's l_id in its zone, a DemoSubGroup's of the same DemoName too: DemoCastSubGroup) and the GeneralPos named by
    them (MR::findNamePos, in the demo's zone first).
    Layout (little endian): u32 ndemos, npos; npos x {float pos[3], rot[3]}; per demo, 4-aligned:
      Demo   {u32 size; char name[64] (UTF-8 DemoName); s16 sw[5] (SW_FIELDS, the group's); u16 zone;
              u8 nparts, nsub, nplayer, ncam, nact, nwipe, nsnd, nhide; u16 ngroup, ncast} (92 B)
      parts  {u16 total; u8 suspend, talk (its name holds 会話: MR::isDemoPartTalk)}
      sub    {u16 total, step (the main part's step it starts at); u8 main, loop (会話アニメループ), pad[2]}
      player {u8 part, pad; s16 pos (-1 none); char bck[28]}
      cam    {u8 part, target (0 none, 1 the player, 2 a cast), continuous, pad; s16 row; u16 pad;
              u32 cls (camera.py class of "e:<DemoName>[<PartName>]"); s32 start, end; char anim[32]}
      act    {u8 part, type (DemoActionInfo::mActionType); s16 pos; u16 c0, cn (its casts: cast[c0 .. c0 + cn)); char anim[24]}
      wipe   {u8 part, type (0 open 1 close 2 force open 3 force close), kind (WIPES), pad; s32 frame}
      snd    {u8 part, ret (ReturnBgm); s16 wipeout (BgmWipeoutFrame); u32 se (se_ids, ~0 none); char bgm[32]}
      group  {s16 row} x ngroup, cast {s16 row} x ncast, hide {s16 row} x nhide (the casts it makes appear that wait
             dead until then: demo_dead_classes), padded to 4"""
    sheets = {n.rsplit("/", 1)[-1].lower(): b for n, b in B.rarc_files((fsp / "ObjectData" / "DemoSheet.arc").read_bytes())}
    main = [d for d in demos if d["name"] == "DemoGroup" and d.get("sheet")]
    DEMO_CANM.clear()
    en2jp = {e: j for j, e in jp2en.items()}
    row_jp = [en2jp.get(r.get("cls", r["name"])) for r in keep]
    pos_tab, pos_index = [], {}

    def pos_of(name, zone):
        if not name:
            return -1
        cands = [g for g in gpos if g["name"] == name]
        if not cands:
            return -1
        g = next((g for g in cands if g["zone"] == zone), cands[0])
        if id(g) not in pos_index:
            pos_index[id(g)] = len(pos_tab)
            pos_tab.append(struct.pack("<6f", *g["pos"], *g["rot"]))
        return pos_index[id(g)]

    def text(v, n):
        b = (v or "").encode("utf8")[:n - 1]
        return b + b"\0" * (n - len(b))
    out = []
    for d in main:
        sh = {k: sheets.get(f"demo{d['sheet'].lower()}{k}.bcsv", b"") for k in DEMO_SHEETS}
        nrow = {k: len(B.bcsv_column(sh[k], "PartName" if k != "subpart" else "SubPartName")) for k in DEMO_SHEETS}
        cols = lambda k, f, dv="": B.bcsv_column(sh[k], f) or [dv] * nrow[k]
        ints = lambda k, f, dv: int_or(sh[k], f, nrow[k], dv)
        names = cols("time", "PartName")
        parts = [struct.pack("<HBB", max(t, 1), sf != 0, "会話" in (n or ""))  # MR::isDemoPartTalk
                 for t, sf, n in zip(ints("time", "TotalStep", 1), ints("time", "SuspendFlag", 0), names)]
        sub_names = cols("subpart", "SubPartName")
        sub = [struct.pack("<HHBB2x", max(t, 1), max(st, 0), names.index(mp) if mp in names else 0xFF, "会話アニメループ" in (sn or ""))
               for t, mp, st, sn in zip(ints("subpart", "SubPartTotalStep", 1), cols("subpart", "MainPartName"),
                                        ints("subpart", "MainPartStep", 1), sub_names)]
        part = lambda n: names.index(n) if n in names else 0x80 | sub_names.index(n) if n in sub_names else None
        group = [i for i, r in enumerate(keep) if r.get("demo", -1) == d["l_id"] and r["zone"] == d["zone"]]
        for sg in demos:
            if sg["name"] == "DemoSubGroup" and sg["demo_name"] == d["demo_name"]:
                group += [i for i, r in enumerate(keep) if r.get("demo", -1) == sg["l_id"] and r["zone"] == sg["zone"]]
        group += [i for i, r in enumerate(keep) if d["demo_name"] in DEMO_DIRECT_CAST.get(r["name"], ()) and i not in group]
        match = lambda name, cid: [i for i in group if row_jp[i] == name and (cid < 0 or keep[i].get("cast", -1) == cid)]
        recs = {k: [] for k in DEMO_SHEETS[2:]}
        for pn, ps, bk in zip(cols("player", "PartName"), cols("player", "PosName"), cols("player", "BckName")):
            if part(pn) is not None:
                recs["player"].append(struct.pack("<Bxh", part(pn), pos_of(ps, d["zone"])) + text(bk, 28))
        for pn, tn, tc, an, a0, a1, co in zip(cols("camera", "PartName"), cols("camera", "CameraTargetName"),
                                               ints("camera", "CameraTargetCastID", -1), cols("camera", "AnimCameraName"),
                                               ints("camera", "AnimCameraStartFrame", -1), ints("camera", "AnimCameraEndFrame", -1),
                                               ints("camera", "IsContinuous", -1)):
            if part(pn) is None:
                continue
            rows_ = match(tn, tc) if tn else []
            tgt = 0 if not tn else 1 if tn == "マリオ" else 2
            if an and rows_:
                DEMO_CANM.add((keep[rows_[0]]["name"], an.rsplit(".", 1)[0].lower()))
            cls = camera.actor_hash(d["demo_name"], "", f"[{pn}]")
            recs["camera"].append(struct.pack("<BBBxhHIii", part(pn), tgt, co == 1, rows_[0] if rows_ else -1, 0, cls, a0, a1) +
                                  text(an.rsplit(".", 1)[0] if an else "", 32))
        casts = []
        for pn, cn, ci, ty, ps, an in zip(cols("action", "PartName"), cols("action", "CastName"), ints("action", "CastID", -1),
                                          ints("action", "ActionType", 0), cols("action", "PosName"), cols("action", "AnimName")):
            if part(pn) is None:
                continue
            rs = match(cn, ci)
            recs["action"].append(struct.pack("<BBhHH", part(pn), ty, pos_of(ps, d["zone"]), len(casts), len(rs)) + text(an, 24))
            casts += rs
        for pn, wn, wt, wf in zip(cols("wipe", "PartName"), cols("wipe", "WipeName", "フェードワイプ"), ints("wipe", "WipeType", 0),
                                  ints("wipe", "WipeFrame", -1)):
            if part(pn) is not None:
                recs["wipe"].append(struct.pack("<BBBxi", part(pn), wt, WIPES.get(wn, 0), wf))
        for pn, bg, se, rb, bw in zip(cols("sound", "PartName"), cols("sound", "Bgm"), cols("sound", "SystemSe"),
                                      ints("sound", "ReturnBgm", 0), ints("sound", "BgmWipeoutFrame", -1)):
            if part(pn) is not None:
                recs["sound"].append(struct.pack("<BBhI", part(pn), rb != 0, bw, se_ids.get(se, 0xFFFFFFFF)) + text(bg, 32))
        appear = {casts[c0 + k] for _, ty, _, c0, cn in (struct.unpack_from("<BBhHH", a) for a in recs["action"]) if ty == 0
                  for k in range(cn)}
        hide = sorted(i for i in appear if keep[i].get("cls", keep[i]["name"]) in demo_dead_classes())[:255]
        rows_b = struct.pack(f"<{len(group)}h{len(casts)}h{len(hide)}h", *group, *casts, *hide)
        rows_b += b"\0" * (-len(rows_b) % 4)
        body = text(d["demo_name"], 64) + struct.pack("<5hH8BHH", *[v if -2**15 <= v < 2**15 else -1 for v in d["sw"]], d["zone"],
                                                       len(parts), len(sub), len(recs["player"]), len(recs["camera"]),
                                                       len(recs["action"]), len(recs["wipe"]), len(recs["sound"]), len(hide),
                                                       len(group), len(casts))
        body += b"".join(parts) + b"".join(sub) + b"".join(b"".join(recs[k]) for k in DEMO_SHEETS[2:]) + rows_b
        out.append(struct.pack("<I", 4 + len(body)) + body)
    return chunk(b"DEMO", struct.pack("<II", len(out), len(pos_tab)) + b"".join(pos_tab) + b"".join(out))


def se_table():
    """SE name -> JAISoundID from engine/src/se_ids.h (tools/sound.py writes it)."""
    h = Path(__file__).resolve().parent.parent / "engine" / "src" / "se_ids.h"
    return {m[0]: int(m[1], 16) for m in re.findall(r"#define (SE_\w+) (0x[0-9A-Fa-f]+)u", h.read_text())} if h.exists() else {}


def blob_of(chunks):
    blob = b"SMGS" + struct.pack("<II4x", 5, len(chunks)) + b"".join(chunks)
    return blob + b"\0" * (-len(blob) % 16)


def build(fs, galaxy, scenario, ratio=0.99):
    """Everything in one blob: placement, every placed model in full, Mario and his clips (the phase 1 layout)."""
    out, info, ctx = placement(fs, galaxy, scenario)
    fsp, stats = Path(fs), {}
    types = info["types"]
    am = ctx["arcmap"]
    names = sorted({n for ns in ctx["per_stage"].values() for n in ns}, key=lambda t: (t not in ctx["planets"], t))
    out += assets(fsp, [(t, am[t]) for t in names] + [(t, t) for t in SKINNED], ctx["planets"], ratio, stats, fx=True) + \
        [anim_chunk(fsp)]
    return blob_of(out), dict(info, **stats)


def low_lod(fsp, name):
    """Lowest LOD archive of a model (budget.py stream rule): Low, else Middle, else the model itself."""
    return next(a for a in (name + "Low", name + "Middle", name) if (fsp / "ObjectData" / f"{a}.arc").exists()
                or a == name)


def packs(fs, galaxy, scenario, resident, ratio=0.99):
    """Streaming layout of one scenario (phase 0b `stream`): -> ({file name: blob}, info).
    scene.bin   placement chunks of every active stage
    common.bin  models placed in two or more stages (always loaded)
    <Stage>.bin models placed only in that stage, in full, with collision (loaded while the stage is on the zone chain)
    <Stage>.low.bin  that stage's planets at their lowest LOD, no collision, no actors (loaded otherwise)
    Models in `resident` (the resident pack: Mario, coins, ...) are left out.
    info["stream"] = {stage: bytes loaded while the player is in it}: scene + common + its chain in full + the other
    stages low. ponytail: shared models always resident in common.bin; per-zone dedup at run time if that is too big."""
    out, info, ctx = placement(fs, galaxy, scenario)
    fsp, planets, per, up, am = Path(fs), ctx["planets"], ctx["per_stage"], ctx["up"], ctx["arcmap"]
    count = {}
    for st, ns in per.items():
        for n in ns:
            if n not in resident:
                count[n] = count.get(n, 0) + 1
    shared = sorted(n for n, c in count.items() if c > 1)
    files = {"scene.bin": blob_of(out),
             "common.bin": blob_of(assets(fsp, [(n, am[n]) for n in shared], planets, ratio))}
    for st, ns in per.items():
        own = [n for n in ns if count.get(n) == 1]
        files[f"{st}.bin"] = blob_of(assets(fsp, [(n, am[n]) for n in own], planets, ratio))
        files[f"{st}.low.bin"] = blob_of(assets(fsp, [(n, low_lod(fsp, n)) for n in own if n in planets], planets,
                                                ratio, collision=False))
    stream = {}
    for z in per:
        chain = [z]
        while chain[-1] in up:
            chain.append(up[chain[-1]])
        chain = set(chain) | {galaxy}
        stream[z] = len(files["scene.bin"]) + len(files["common.bin"]) + sum(
            len(files[f"{st}.bin" if st in chain else f"{st}.low.bin"]) for st in per)
    info.update(stream=stream, whole=sum(len(b) for f, b in files.items() if not f.endswith(".low.bin")))
    return files, info


def resident_pack(fs, ratio=0.99):
    """Models every scenario shares (phase 0b RESIDENT set, the ones the converter knows) + Mario + his clips."""
    fsp = Path(fs)
    names = list(RESIDENT) + list(SKINNED)
    models = [(t, t) for t in names] + [(f"{c}@{k}", f"{c}@{k}") for c in MAT_COLOR for k in range(len(MAT_COLOR[c][1]))]
    return blob_of(assets(fsp, models, {}, ratio, fx=True) + [anim_chunk(fsp), eye_chunk(fsp)]), set(names)


def read_chunks(blob):
    """Parse a blob back: -> {tag: payload}. Used by the selftest and handy for checking files."""
    assert blob[:4] == b"SMGS", blob[:4]
    n = struct.unpack_from("<I", blob, 8)[0]
    o, out = 16, {}
    for _ in range(n):
        tag, size = blob[o:o + 4], struct.unpack_from("<I", blob, o + 4)[0]
        out[tag] = blob[o + 16:o + 16 + size]
        o += 16 + size
    return out


def selftest():
    fp = Path(__file__).resolve().parent.parent / "data/fs/files/ObjectData/FlipPanel.arc"
    if fp.exists():  # its three faces overlap: the one its joints show (rest: "?", PanelA: "!", PanelEnd: End)
        faces = lambda pose: [len(g[1]) // 3 for g in model_groups(fp, 1.0, pose=pose)[0]
                              if not (g[3] and g[3].get("env"))]  # env layers (tex.env_layer) aside
        assert len(faces(None)) == len(faces("PanelA")) == len(faces("PanelEnd")) == 2
        assert len({faces(None)[1], faces("PanelA")[1], faces("PanelEnd")[1]}) == 3
    # a unit right triangle mapped onto a 64 x 64 texture: 64 texels per unit -> K = 16 log2(64 / LOD_F)
    sq = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    full = lod_sums(sq, [0, 1, 2], [(0, 0), (1, 0), (0, 1)], {"w": 64, "h": 64})
    assert lod_k(full) == round(16 * math.log2(64 / LOD_F))
    # a group on a corner of the same texture (FlipPanel's edge) must not drag the texture to its smallest mip
    tiny = lod_sums(sq, [0, 1, 2], [(0, 0), (0.01, 0), (0, 0.01)], {"w": 64, "h": 64})
    both = [x + y for x, y in zip(full, tiny)]
    assert lod_k(tiny) < lod_k(both) < lod_k(full), (lod_k(tiny), lod_k(both), lod_k(full))
    # BCSV with name (string) + pos_x (float) + Obj_arg0 (long): 2 rows
    fields = [("name", 0, 6), ("pos_x", 4, 2), ("Obj_arg0", 8, 0)]
    hdr = struct.pack(">4I", 2, 3, 16 + 36, 12) + b"".join(
        struct.pack(">IIHBB", B.jmap_hash(f), 0xFFFFFFFF, o, 0, t) for f, o, t in fields)
    strings = b"Coin\0Kuribo\0"
    data = struct.pack(">If i", 0, 1.5, 7) + struct.pack(">Ifi", 5, -2.0, -1)
    b = hdr + data + strings
    assert float_column(b, "pos_x") == [1.5, -2.0]
    r = rows(b)
    assert [x["name"] for x in r] == ["Coin", "Kuribo"] and r[0]["args"][0] == 7 and r[1]["pos"][0] == -2.0, r
    assert r[0]["scale"] == [1.0, 1.0, 1.0] and r[0]["path"] == -1
    # Bezier rails: a straight part stays two points, a curved one is sampled (midpoint of this arch: y = 37.5)
    line = [(0, 0, 0), (100, 0, 0)]
    assert bezier_polyline(line, line, line, 0) == line
    arch = bezier_polyline(line, [(0, 0, 0), (100, 50, 0)], [(0, 50, 0), (100, 0, 0)], 0)
    assert len(arch) == BEZIER_STEPS + 1 and abs(arch[BEZIER_STEPS // 2][1] - 37.5) < 1e-6, arch
    # round trip of the chunk framing
    blob = b"SMGS" + struct.pack("<II4x", 2, 2) + chunk(b"TYPE", struct.pack("<I", 1) + name32("Coin")) + chunk(b"KCL ", b"x")
    c = read_chunks(blob)
    assert c[b"TYPE"][4:8] == b"Coin" and c[b"KCL "][:1] == b"x", c
    for rot in ([10, 20, 30], [-45, 80, 170], [0, 0, 90]):
        assert all(abs(x - y) < 1e-6 for x, y in zip(mtx_euler(rot_mtx(rot)), rot)), rot
    zs = [dict(pos=[0, 0, 0], rot=[0, 0, 0]), dict(pos=[100, 0, 0], rot=[0, 90, 0])]
    it = [dict(zone=1, pos=[10, 0, 0], rot=[0, 0, 0]), dict(zone=1, pts=[(0, 0, 10)])]
    to_world(zs, {}, it)
    assert all(abs(x - y) < 1e-6 for x, y in zip(it[0]["pos"], [100, 0, -10])), it[0]
    assert abs(it[0]["rot"][1] - 90) < 1e-6 and abs(it[1]["pts"][0][0] - 110) < 1e-6, it
    g = grav_record(dict(name="GlobalCubeGravity", pos=[0, 0, 0], rot=[0, 0, 0], scale=[2, 1, 1], args=[3, 1, 2, -1],
                         distant=None, range=-1.0, priority=0, inverse=0))
    v = struct.unpack("<HHiiff12f4fi", g)
    assert v[1] == 0b100111 and v[6:9] == (1000.0, 0.0, 0.0) and v[15:18] == (0.0, 500.0, 0.0), v
    # two triangles sharing an edge: 6 emitted, 4 unique; colours keep their rgb, the 4th byte is the index
    P = [struct.pack("<4h", *v, 0) for v in ((0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0))]
    nb, body = index_batches(b"".join(P), b"\x10\x20\x30\x80" * 6, b"", 6)
    assert nb == 1 and struct.unpack_from("<HH", body, 0) == (6, 4), body[:4]
    assert body[16 + 32 + 3::4][:6] == bytes((0, 1, 2, 1, 2, 3)), body
    # event stars: the observer's arg7 4 names star 4; a cage with a star (arg7 7) the scenario's whose declarer it is
    st = lambda n: dict(name="PowerStar", args=[n] + [2**32 - 1] * 7)
    keep = [st(4), dict(name="FlipPanelObserver", args=[2**32 - 1] * 7 + [4]), st(1),
            dict(name="BreakableCage", args=[2**32 - 1] * 7 + [7]), st(2)]
    declare_stars(keep, 2, {1: "籠", 2: COIN100}, {"BreakableCage": "籠"})
    assert [s32(r["args"][7]) for r in keep] == [1, 4, 3, 7, -2], keep
    print("selftest ok")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        ratio = next((float(a.split("=")[1]) for a in sys.argv if a.startswith("--simplify=")), 0.99)
        args = [a for a in sys.argv[1:] if not a.startswith("--")]
        blob, info = build(args[0], args[1], int(args[2]), ratio)
        Path(args[3]).parent.mkdir(parents=True, exist_ok=True)
        Path(args[3]).write_bytes(blob)
        print(info)
        chunks = read_chunks(blob)
        k = chunks.get(b"KCL ", b"")
        if len(k) > 44:  # little-endian check: the first header word is the pos offset 0x38
            assert struct.unpack_from("<I", k, 40)[0] == 0x38, "KCL in the blob is not little-endian"
        sizes = {k.decode(): len(v) for k, v in chunks.items()}
        print(f"blob {len(blob)} B ({len(blob) / 2**20:.2f} MB), simplify R = {ratio}, chunks {sizes}")
