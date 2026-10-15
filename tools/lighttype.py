"""Actor light type per placed class, from Petari's sources (vendor/Petari): the class NameObjFactory creates for the
name, the first MR::connectToScene* call in its source (MapObjActor: connectToSceneMapObj, or its collision
variant) and the draw buffer that call names, whose light type is cDrawBufferListInitTable's
(SceneNameObjListExecutor.cpp). Written to tools/light_types.json (committed: the build does not need Petari).

usage: python tools/lighttype.py            (regenerate tools/light_types.json)
       python tools/lighttype.py --selftest
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PETARI = ROOT / "vendor" / "Petari"
OUT = Path(__file__).resolve().parent / "light_types.json"
TYPES = {"LightType_Player": 0, "LightType_Strong": 1, "LightType_Weak": 2, "LightType_Planet": 3}


def buffer_lights():
    """DrawBufferType_X -> light type (cDrawBufferListInitTable)."""
    s = (PETARI / "src/Game/Scene/SceneNameObjListExecutor.cpp").read_text(encoding="utf-8", errors="replace")
    s = s[s.index("cDrawBufferListInitTable"):]
    return {b: TYPES.get(l, -1) for b, l in re.findall(r"MR::(DrawBufferType_\w+),\s*\d+,\s*MR::(LightType_\w+)", s)}


def connect_buffers():
    """MR::connectToSceneXxx -> its DrawBufferType_X (ObjUtil.cpp)."""
    s = (PETARI / "src/Game/Util/ObjUtil.cpp").read_text(encoding="utf-8", errors="replace")
    return dict(re.findall(r"void MR::(connectToScene\w*)\([^)]*\)\s*\{[^}]*?MR::connectToScene\([^,]+,[^,]+,[^,]+,\s*MR::(DrawBufferType_\w+)", s))


def factory():
    """placement name -> C++ class, or "create<X>" for a creator function (NameObjFactory.cpp's table). Coin and
    PurpleCoin come from their own creators (Coin.cpp)."""
    s = (PETARI / "src/Game/NameObj/NameObjFactory.cpp").read_text(encoding="utf-8", errors="replace")
    out = dict(re.findall(r'"(\w+)",\s*create\w*<\s*(\w+)\s*>', s))
    for name, fn in re.findall(r'"(\w+)",\s*MR::(create\w+)', s):
        out.setdefault(name, fn)
    out.update(Coin="Coin", PurpleCoin="Coin")
    return out


def sources():
    """class / creator function -> its source file (a creator lives in its class's file)."""
    out = {}
    for p in (PETARI / "src").rglob("*.cpp"):
        out.setdefault(p.stem, p)
        for fn in re.findall(r"NameObj\*\s+(create\w+)\(", p.read_text(encoding="utf-8", errors="replace")):
            out.setdefault(fn, p)
    return out


def class_buffer(cls, src, conn, seen=None):
    """The draw buffer of a class: its source's first connectToScene* call, else its base class's."""
    seen = seen or set()
    if cls in seen or cls not in src:
        return None
    seen.add(cls)
    s = src[cls].read_text(encoding="utf-8", errors="replace")
    m = re.search(r"MR::(connectToScene\w*)\(\s*this\s*(?:,[^,]+,[^,]+,\s*MR::(DrawBufferType_\w+))?", s)
    if m:
        return m.group(2) if m.group(1) == "connectToScene" else conn.get(m.group(1))
    if "MapObjActor::connectToScene" in s or re.search(r"initialize\w*\(\s*rIter", s) and "MapObjActor" in s:
        return "DrawBufferType_MapObj"
    h = next((PETARI / "include").rglob(f"{cls}.hpp"), None)
    base = re.search(rf"class\s+{cls}\s*:\s*public\s+(\w+)", h.read_text(encoding="utf-8", errors="replace")) if h else None
    if base and base.group(1) in ("MapObjActor", "MapObjActorUseRailMover", "LiveActor") and base.group(1) != "LiveActor":
        return "DrawBufferType_MapObj"
    return class_buffer(base.group(1), src, conn, seen) if base else None


def build():
    lights, conn, fac, src = buffer_lights(), connect_buffers(), factory(), sources()
    out = {}
    for name, cls in sorted(fac.items()):
        b = class_buffer(cls, src, conn)
        if b in lights and lights[b] >= 0:
            out[name] = lights[b]
    return out


def selftest():
    t = json.loads(OUT.read_text())
    assert t["Kuribo"] == 1 and t["Coin"] != 0, (t.get("Kuribo"), t.get("Coin"))
    print("selftest ok", len(t))


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    else:
        t = build()
        OUT.write_text(json.dumps(t, indent=0, sort_keys=True) + "\n")
        print(len(t), "classes;", {v: sum(1 for x in t.values() if x == v) for v in range(4)})
