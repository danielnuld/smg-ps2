"""Camera areas and camera parameters of a scenario -> CARE and CAMS blob chunks (phase 2, task 4.1).

Areas (`jmp/placement/<layer>/cameracubeinfo`, NameObjFactory.cpp:63-75, AreaForm.cpp, CubeCamera.cpp):
- CubeCameraBox = centre-origin cube, 1000 x scale per axis, rotated by the placement rotation (AreaForm.cpp:45-54,
  104-125); CubeCameraCylinder = base-origin cylinder, radius 500 x scale_x, height 500 x scale_y along the rotated
  Y axis (AreaForm.cpp:241-313); CubeCameraSphere = radius 500 x scale_x (AreaForm.cpp:160-166, 277-285).
- Obj_arg0 = camera id ("c:%04x" in the same zone, CameraParamChunkID.cpp:74), Obj_arg2 = priority: the manager
  selection-sorts by it (CubeCamera.cpp:86-110, reproduced here so ties land in the same order) and the last area
  that contains the target wins (AreaObj.cpp:111-125). Obj_arg3 = category (CubeCamera.cpp:38-45): mask =
  1 << (arg & 15) when arg >> 4 == 0, else arg >> 4; the normal game category is bit 0 (CubeCameraArea::
  ECategory_Normal, CameraManGame.cpp:561). Only areas whose mask has bit 0 are kept.
- Validity "Invalid" or a SW_A switch makes an area start invalid (CubeCamera.cpp:15-28); those are dropped.

CARE: u32 n, 12 pad; n x {u8 shape (0 box, 1 cylinder, 2 sphere), u8 zone, u16 camera (index into CAMS, 0xFFFF if
missing), f32 pos[3], rot[3] (degrees), scale[3]} (40 B), in selection order.

Cameras (`camera/cameraparam.bcam`, a JMap table; field names CameraParamChunk.cpp:141-200, vectors as "name.X/Y/Z",
DotCamParams.cpp:62-66). Kept: game cameras "c:%04x" (areas), "s:%04x" (start), "o:" (named defaults); event cameras
("e:") and ground-triangle cameras ("g:") are not used yet. Missing fields take CameraParamChunk.hpp:12-55 defaults.
angleA / angleB are radians as stored (each type converts them its own way).
CAMS: u32 n, 12 pad; n x 128 B {u16 zone, u8 kind ('c', 's', 'o'), u8 type (TYPES index, 255 = other: the engine uses
XZ_PARA like CameraHolder's default), u16 num (hex id, 0 for "o:"), u16 flags (bit i = flag.<FLAGS[i]>), f32 dist,
angleA, angleB, wpoint[3], axis[3], up[3], woffset[3], loffset, loffsetv, fovy, upper, lower, uplay, lplay,
s32 camint, num1, num2, vpanuse, f32 vpanaxis[3], pad}.
"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import budget as B

SHAPES = {"CubeCameraBox": 0, "CubeCameraCylinder": 1, "CubeCameraSphere": 2}


def category_mask(arg):
    arg = 0 if arg < 0 or arg >= 2**31 else arg
    return arg >> 4 if arg >> 4 else 1 << (arg & 15)


def areas(rows):
    """rows: placement rows (slice.rows + zone) of cameracubeinfo -> kept areas in Petari's selection order."""
    out = []
    for r in rows:
        if r["name"] not in SHAPES or not category_mask(r["args"][3]) & 1:
            continue
        if r.get("invalid") or r.get("sw_a", -1) >= 0:
            continue
        out.append(r)
    for i in range(len(out) - 1):   # CubeCameraMgr::sort: selection sort on Obj_arg2 (signed)
        s = lambda r: r["args"][2] - 2**32 if r["args"][2] >= 2**31 else r["args"][2]
        k = i
        for j in range(i + 1, len(out)):
            if s(out[k]) > s(out[j]):
                k = j
        if k != i:
            out[i], out[k] = out[k], out[i]
    return out


TYPES = ("CAM_TYPE_XZ_PARA", "CAM_TYPE_TOWER", "CAM_TYPE_FOLLOW", "CAM_TYPE_WONDER_PLANET", "CAM_TYPE_EYEPOS_FIX",
         "CAM_TYPE_POINT_FIX", "CAM_TYPE_EYEPOS_FIX_THERE", "CAM_TYPE_CHARMED_FIX", "CAM_TYPE_TOWER_POS",
         "CAM_TYPE_FRONT_AND_BACK", "CAM_TYPE_TRUNDLE", "CAM_TYPE_CUBE_PLANET", "CAM_TYPE_OBJ_PARALLEL")


def class_hash(name):
    """The engine's hash of an (English) class name: h = h x 31 + c over its bytes (camera.c event cameras)."""
    h = 0
    for ch in name.encode("ascii", "replace"):
        h = (h * 31 + ch) & 0xFFFFFFFF
    return h
ACTOR_CAM = re.compile(r"(.+?)(固有|共通)(\D*)(\d+)$")


def actor_hash(en, mark, event):
    """An actor camera's class: the hash of "<English class><固有 | 共通><event>" over its UTF-8 bytes (camera.c
    camera_actor_start hashes the same string)."""
    h = 0
    for ch in (en + mark + event).encode("utf-8"):
        h = (h * 31 + ch) & 0xFFFFFFFF
    return h
FLAGS = ("noreset", "nofovy", "lofserpoff", "antibluroff", "collisionoff", "subjectiveoff")  # CameraParamChunk.cpp:14
FLOATS = (("dist", 1200.0), ("angleA", 0.0), ("angleB", 0.3), ("wpoint", (0, 0, 0)), ("axis", (0, 1, 0)),
          ("up", (0, 1, 0)), ("woffset", (0, 100, 0)), ("loffset", 0.0), ("loffsetv", 0.0), ("fovy", 45.0),
          ("upper", 0.3), ("lower", 0.1), ("uplay", 300.0), ("lplay", 800.0))
INTS = (("camint", 120), ("num1", 0), ("num2", 0), ("vpanuse", 1))


def cameras(bcams, float_column, int_column, jp2en=None):
    """bcams: [(zone, bcam bytes)] -> (CAMS entries as bytes, {(zone, kind, num): index}, Counter of types used)."""
    from collections import Counter
    out, index, used = [], {}, Counter()
    for zone, bc in bcams:
        ids = B.bcsv_column(bc, "id")
        n = len(ids)
        if not n:
            continue
        types = B.bcsv_column(bc, "camtype")
        col = {}
        def get(name, default):
            if name not in col:
                v = float_column(bc, name) or int_column(bc, name, n, None)
                col[name] = v if v and len(v) == n and v[0] is not None else None
            return col[name]
        for i, cid in enumerate(ids):
            kind = cid[:2]
            evclass = 0
            if kind == "e:" and ACTOR_CAM.match(cid[2:]):  # actor cameras "e:<actor>固有|共通<event><CameraSetId>"
                actor, mark, event, digits = ACTOR_CAM.match(cid[2:]).groups()  # (ActorCameraUtil createMultiActorCameraName)
                en = (jp2en or {}).get(actor)
                if not en:
                    continue
                evclass, num = actor_hash(en, mark, event), int(digits) + (0x8000 if mark == "共通" else 0)
            elif kind == "e:" and cid.endswith("]"):  # a demo's part "e:<DemoName>[<PartName>]" (DemoCameraKeeper)
                evclass, num = actor_hash(cid[2:], "", ""), 0
            elif kind == "e:" and ":" in cid[2:]:  # MultiEventCamera "e:<actor>:<CameraSetId 3 digits>:<NN>番目": its class and set << 4 | NN
                parts = cid[2:].split(":")
                en = (jp2en or {}).get(parts[0]) if len(parts) == 3 else None
                if not en or not parts[1].isdigit() or not parts[2][:2].isdigit():
                    continue
                evclass, num = class_hash(en), int(parts[1]) << 4 | int(parts[2][:2])
            elif kind == "e:":  # a named one ("e:引き戻し" declareGlobalEventCamera, "e:ワープカメラ 0-A" WarpPod): its name's hash
                evclass, num = actor_hash(cid[2:], "", ""), 0
            elif kind not in ("c:", "s:", "o:"):
                continue
            else:
                num = int(cid[2:], 16) if kind != "o:" else 0
            t = types[i] if i < len(types) else ""
            used[t] += 1
            fl = []
            for name, d in FLOATS:
                if isinstance(d, tuple):
                    fl += [get(f"{name}.{a}", 0) and get(f"{name}.{a}", 0)[i] if get(f"{name}.{a}", 0) else d[k]
                           for k, a in enumerate("XYZ")]
                else:
                    c = get(name, d)
                    fl.append(float(c[i]) if c else d)
            if t == "CAM_TYPE_PLANET" and get("angleA", 0) is None:
                fl[1] = 30.0
            ints = [int(get(name, d)[i]) if get(name, d) else d for name, d in INTS]
            pan = [get(f"vpanaxis.{a}", 0)[i] if get(f"vpanaxis.{a}", 0) else (0, 1, 0)[k] for k, a in enumerate("XYZ")]
            flags = sum((1 << k) for k, f in enumerate(FLAGS) if get(f"flag.{f}", 0) and get(f"flag.{f}", 0)[i])
            if get("eflag.enableErpFrame", 0) and get("eflag.enableErpFrame", 0)[i]:  # an event's (CameraParamChunkEvent)
                flags |= 64
            if get("eflag.enableEndErpFrame", 0) and get("eflag.enableEndErpFrame", 0)[i]:  # camendint + 1 in bits 7..15
                flags |= min((int(get("camendint", 120)[i]) if get("camendint", 120) else 120) + 1, 511) << 7
            evfrm = get("evfrm", -1)
            evfrm = int(evfrm[i]) if evfrm else -1
            evfrm = evfrm - 2**32 if evfrm >= 2**31 else evfrm
            index[(zone, kind[0], num)] = len(out)
            out.append(struct.pack("<HBBHH22f4i3fiI", zone, ord(kind[0]), TYPES.index(t) if t in TYPES else 255, num & 0xFFFF,
                                   flags, *fl, *ints, *pan, evfrm, evclass))
    return out, index, used


def selftest():
    assert category_mask(-1) == 1 and category_mask(0) == 1 and category_mask(256) == 16 and category_mask(2) == 4
    rows = [dict(name="CubeCameraBox", args=[5, 0, 3, -1]), dict(name="CubeCameraSphere", args=[6, 0, 1, -1]),
            dict(name="CubeCameraCylinder", args=[7, 0, 2, 256]), dict(name="LightCtrlCube", args=[0, 0, 0, -1])]
    assert [r["args"][0] for r in areas(rows)] == [6, 5], areas(rows)
    print("selftest ok")


if __name__ == "__main__":
    selftest()
