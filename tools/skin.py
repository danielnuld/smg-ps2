"""J3D skeletons: joints, draw matrices (rigid / weighted envelopes) and per-group matrix tables (phase 2, task 3.1).

Layouts (J3DModelLoader.hpp, J3DJointFactory.hpp, J3DShapeFactory.hpp):
- JNT1 +8 u16 count, +0xC init data (0x40 each: u16 kind @0, J3DTransformInfo @4 = scale f32x3, rot s16x3 (+pad),
  trans f32x3), +0x10 u16 remap, +0x14 name table. Parents come from the INF1 hierarchy (0x10 joint, 0x01 open,
  0x02 close).
- DRW1 +8 u16 count, +0xC u8 weighted[], +0x10 u16 index[] (joint if not weighted, else envelope).
- EVP1 +8 u16 count, +0xC u8 mix count[], +0x10 u16 joint[], +0x14 f32 weight[], +0x18 inverse bind 3x4 per joint.
- SHP1 +0x1C u16 matrix table, +0x24 J3DShapeMtxInitData {u16 use, u16 count, u32 first}; shape init @6 = first
  matrix init entry; a group's PNMTXIDX / 3 picks from its list, 0xFFFF = the previous group's entry (J3DShapeMtxMulti).

Spaces: a weighted draw matrix is sum(w * world[j] * invbind[j]) and its vertices are in model space; a rigid one is
world[j] and its vertices are in that joint's space (J3DMtxBuffer: anm matrix used as is). Joint local matrix =
T * Rz * Ry * Rx * S (J3DGetTranslateRotateMtx order). ponytail: scale compensation (Maya segment scale) ignored.
"""
import math
import struct

import numpy as np

from budget import u16, u32


def rot_mtx(rx, ry, rz):
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    return np.array([[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
                     [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
                     [-sy, cy * sx, cy * cx]])


def local(scale, rot, trans):
    """-> 4x4 = T * R * S (rot in radians)."""
    m = np.eye(4)
    m[:3, :3] = rot_mtx(*rot) * np.asarray(scale)[None, :]
    m[:3, 3] = trans
    return m


def joints(b, bl):
    """-> [dict(name, parent, scale, rot (rad), trans)] in JNT1 order."""
    j = bl[b"JNT1"]
    n = u16(b, j + 8)
    data, remap, nt = (j + u32(b, j + k) for k in (0x0C, 0x10, 0x14))
    names = []
    for i in range(u16(b, nt)):
        s = nt + u16(b, nt + 4 + 4 * i + 2)
        names.append(b[s:b.index(b"\0", s)].decode("shift_jis"))
    out = []
    for i in range(n):
        e = data + 0x40 * u16(b, remap + 2 * i)
        sc = struct.unpack_from(">3f", b, e + 4)
        r = [x * math.pi / 0x8000 for x in struct.unpack_from(">3h", b, e + 0x10)]
        t = struct.unpack_from(">3f", b, e + 0x18)
        out.append(dict(name=names[i] if i < len(names) else f"j{i}", parent=-1, scale=sc, rot=r, trans=t,
                        ssc=b[e + 2] == 1))
    inf = bl[b"INF1"]
    if u16(b, inf + 8) & 0xF != 2:  # scale compensation only under the Maya calc type (INF1 flags)
        for j in out:
            j["ssc"] = False
    p, cur, stack = inf + u32(b, inf + 0x14), -1, []
    last = -1
    while True:
        t, i = u16(b, p), u16(b, p + 2)
        if t == 0:
            break
        if t == 0x01:
            stack.append(cur)
            cur = last
        elif t == 0x02:
            cur = stack.pop()
        elif t == 0x10:
            out[i]["parent"] = cur
            last = i
        p += 4
    return out


def unit(v):
    l = np.linalg.norm(v)
    return v / l if l > 1e-9 else v


def world(js, locals_=None):
    """World matrix per joint (parents come before children in JNT1 order in J3D files; checked)."""
    w = [None] * len(js)
    for i, j in enumerate(js):
        m = locals_[i] if locals_ is not None else local(j["scale"], j["rot"], j["trans"])
        assert j["parent"] < i, "parent after child"
        if j.get("ssc") and j["parent"] >= 0:  # Maya segment scale compensate: the parent's scale left out
            m = m.copy()
            m[:3, :3] = np.diag([1.0 / x for x in js[j["parent"]]["scale"]]) @ m[:3, :3]
        w[i] = m if j["parent"] < 0 else w[j["parent"]] @ m
    return w


def draw_entries(b, bl):
    """-> [(joint, None) for rigid | (None, [(joint, weight, invbind 4x4)])] per DRW1 entry."""
    d = bl[b"DRW1"]
    n = u16(b, d + 8)
    wt = b[d + u32(b, d + 0xC): d + u32(b, d + 0xC) + n]
    idx = struct.unpack_from(f">{n}H", b, d + u32(b, d + 0x10))
    env = []
    if b"EVP1" in bl:
        e = bl[b"EVP1"]
        ne = u16(b, e + 8)
        cnt, ji, wi, ib = (e + u32(b, e + k) for k in (0x0C, 0x10, 0x14, 0x18))
        k = 0
        for x in range(ne):
            mix = []
            for _ in range(b[cnt + x]):
                j = u16(b, ji + 2 * k)
                m = np.eye(4)
                m[:3, :] = np.array(struct.unpack_from(">12f", b, ib + 48 * j)).reshape(3, 4)
                mix.append((j, struct.unpack_from(">f", b, wi + 4 * k)[0], m))
                k += 1
            env.append(mix)
    return [(idx[i], None) if not wt[i] else (None, env[idx[i]]) for i in range(n)]


def draw_mtx(entries, w):
    """Draw matrices for joint world matrices w."""
    out = []
    for j, mix in entries:
        if mix is None:
            out.append(w[j])
        else:
            out.append(sum(wt * (w[jj] @ ib) for jj, wt, ib in mix))
    return out


def group_tables(b, bl):
    """-> {(shape, group): [DRW1 index per matrix slot]}, 0xFFFF resolved to the previous group's entry."""
    s = bl[b"SHP1"]
    init, table, minit = s + u32(b, s + 0x0C), s + u32(b, s + 0x1C), s + u32(b, s + 0x24)
    out = {}
    for sh in range(u16(b, s + 8)):
        si = init + 0x28 * sh   # SHP1 shapes are indexed through the remap table; Mario's is identity (checked)
        ng, m0 = u16(b, si + 2), u16(b, si + 6)
        prev = []
        for g in range(ng):
            use, cnt, first = struct.unpack_from(">HHI", b, minit + 8 * (m0 + g))
            lst = list(struct.unpack_from(f">{cnt}H", b, table + 2 * first)) if cnt else [use]
            lst = [prev[k] if v == 0xFFFF and k < len(prev) else v for k, v in enumerate(lst)]
            out[(sh, g)] = lst
            prev = lst
    return out


def hermite(f, t0, v0, m0, t1, v1, m1):
    """JMAHermiteInterpolation (J3DAnimation.cpp:512-552): tangents per frame."""
    d = t1 - t0
    t = (f - t0) / d
    return (v0 + t * d * m0 + t * t * (3 * (v1 - v0) - 2 * d * m0 - d * m1)
            + t * t * t * (2 * (v0 - v1) + d * m0 + d * m1))


def key(f, count, typ, data):
    """J3DGetKeyFrameInterpolation (J3DAnimation.cpp:555-602) on a flat key list: type 0 = (time, value, tangent),
    type 1 = (time, value, in, out); count 0 -> None (default), 1 -> constant."""
    if count == 1:
        return data[0]
    st = 3 if typ == 0 else 4
    if f < data[0]:
        return data[1]
    last = (count - 1) * st
    if data[last] <= f:
        return data[last + 1]
    i = 0
    while i + st <= last and data[i + st] <= f:
        i += st
    out0 = data[i + 2] if typ == 0 else data[i + 3]
    in1 = data[i + st + 2]
    return hermite(f, data[i], data[i + 1], out0, data[i + st], data[i + st + 1], in1)


def clip_pose(clip, f, bind):
    """Little-endian clip (slice.anim_clip) at frame f -> [(scale, rot rad, trans)] per joint. ponytail: frames past
    the end clamp; loop modes are the engine's business."""
    fmax, attr, shift, nj, ns, nr, nt = struct.unpack_from("<HBBHHHH", clip, 0)
    tab = struct.unpack_from(f"<{nj * 27}H", clip, 16)
    o = 16 + nj * 54
    o += -o % 4
    sc = struct.unpack_from(f"<{ns}f", clip, o); o += 4 * ns
    ro = struct.unpack_from(f"<{nr}h", clip, o); o += 2 * nr; o += -o % 4
    tr = struct.unpack_from(f"<{nt}f", clip, o)
    out = []
    for j in range(nj):
        s3, r3, t3 = [], [], []
        for ax in range(3):
            e = tab[(3 * j + ax) * 9:(3 * j + ax) * 9 + 9]
            for (cnt, off, typ), arr, dst, dflt in (((e[0], e[1], e[2]), sc, s3, 1.0), ((e[3], e[4], e[5]), ro, r3, 0.0),
                                                   ((e[6], e[7], e[8]), tr, t3, 0.0)):
                st = 1 if cnt == 1 else (3 if typ == 0 else 4)
                v = dflt if cnt == 0 else key(f, cnt, typ, arr[off:off + cnt * st])
                dst.append(v)
        out.append((s3, [int(v) * (1 << shift) * math.pi / 0x8000 for v in r3], t3))
    return out


def selftest():
    m = local((1, 1, 1), (0, 0, math.pi / 2), (5, 0, 0))
    assert np.allclose(m @ [1, 0, 0, 1], [5, 1, 0, 1]), m       # Rz 90: x -> y, then translate
    js = [dict(parent=-1, scale=(1, 1, 1), rot=(0, 0, 0), trans=(0, 10, 0)),
          dict(parent=0, scale=(1, 1, 1), rot=(0, 0, 0), trans=(0, 5, 0))]
    w = world(js)
    assert np.allclose(w[1][:3, 3], [0, 15, 0])
    ib = np.linalg.inv(w[1])
    d = draw_mtx([(0, None), (None, [(1, 0.5, ib), (0, 0.5, np.linalg.inv(w[0]))])], w)
    assert np.allclose(d[1], np.eye(4)), d[1]                    # bind pose: weighted matrix is identity
    # Hermite: tangents equal to the slope give a straight line
    assert abs(hermite(5, 0, 0, 1, 10, 10, 1) - 5) < 1e-9
    assert key(15, 2, 0, [0, 0, 1, 10, 10, 1]) == 10 and key(-1, 2, 0, [0, 3, 0, 10, 4, 0]) == 3
    print("selftest ok")


if __name__ == "__main__":
    selftest()
