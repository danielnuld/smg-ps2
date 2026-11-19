"""MapObjActor::appear per object name, read from Petari's sources: which placed names are a MapObjActor whose
appear() is MapObjActor's (its Appear effect, StageEffect start SE) and which of those chime (mIsAppearRiddleSE: _B6).

flags(name) -> bit 0: created as a MapObjActor (sub)class that keeps MapObjActor::appear (an override that calls
MapObjActor::appear() counts); bit 1: its init sets up the appear riddle SE (setupInitInfoSimpleMapObj / setupSeAppear,
not undone by setupNoAppearRiddleSE / mIsAppearRiddleSE = false, nor by setupInitInfoTypical's cNoAppearRiddleSENameList).
slice.py writes them to the last byte of the name's TYPE entry (game.c type_flags); usage: python tools/mapobj_flags.py [Name ...]
ponytail: a class's init is read as text (the first `void Class::init(` body, else its base's); a setup done in a helper
the init calls is not followed."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "vendor/Petari"
_cache = {}


def _load():
    if _cache:
        return _cache
    fac = (ROOT / "src/Game/NameObj/NameObjFactory.cpp").read_text(encoding="utf-8")
    i, j = fac.index("cCreateTable[]"), fac.index("cName2ArchiveNamesTable[]")
    cls_of = {n: c for n, c in re.findall(r'\{\s*"([^"]+)",\s*createNameObj<\s*(\w+)\s*>', fac[i:j])}
    base, inits, appears = {}, {}, {}
    for f in (ROOT / "include/Game").rglob("*.hpp"):
        for c, b in re.findall(r"class\s+(\w+)\s*:\s*public\s+(\w+)", f.read_text(encoding="utf-8", errors="replace")):
            base.setdefault(c, b)
    for f in (ROOT / "src/Game").rglob("*.cpp"):
        t = f.read_text(encoding="utf-8", errors="replace")
        for m in re.finditer(r"^void (\w+)::(init|appear)\((?:const JMapInfoIter&[^)]*)?\)\s*\{(.*?)\n\}\n", t, re.S | re.M):
            (inits if m.group(2) == "init" else appears).setdefault(m.group(1), m.group(3))
    typ = (ROOT / "src/Game/MapObj/MapObjActorInitInfo.cpp").read_text(encoding="utf-8")
    k = typ.index("cNoAppearRiddleSENameList")
    no_riddle = set(re.findall(r'"(\w+)"', typ[k:typ.index("};", k)]))
    _cache.update(cls_of=cls_of, base=base, inits=inits, appears=appears, no_riddle=no_riddle)
    return _cache


def flags(name):
    d = _load()
    c = d["cls_of"].get(name)
    chain = []
    while c and c not in chain:
        chain.append(c)
        c = d["base"].get(c)
    if "MapObjActor" not in chain:
        return 0
    chain = chain[:chain.index("MapObjActor")]
    for c in chain:  # an appear() of its own below MapObjActor: kept only if it calls up
        body = d["appears"].get(c)
        if body is not None and not re.search(r"\b(MapObjActor|%s)::appear\(\)" % "|".join(chain), body):
            return 0
    out = 1
    init = next((d["inits"][c] for c in chain if c in d["inits"]), "")
    on = [m.start() for m in re.finditer(r"setupInitInfoSimpleMapObj\(|setupSeAppear\(\)", init)]
    off = [m.start() for m in re.finditer(r"setupNoAppearRiddleSE\(|mIsAppearRiddleSE = false", init)]
    if on and not (off and max(off) > max(on)):
        if not ("setupInitInfoTypical(" in init and name in d["no_riddle"]):
            out |= 2
    return out


def all_flags():
    """{name: flags} of every name the factory creates with a flag set; kept in the temp directory (reading Petari's
    sources through a slow disk takes minutes), made again when NameObjFactory.cpp is newer."""
    import json
    import tempfile
    fac = ROOT / "src/Game/NameObj/NameObjFactory.cpp"
    cache = Path(tempfile.gettempdir()) / "smg_mapobj_flags.json"
    if cache.exists() and cache.stat().st_mtime > max(fac.stat().st_mtime, Path(__file__).stat().st_mtime):
        return json.loads(cache.read_text())
    out = {n: flags(n) for n in _load()["cls_of"]}
    out = {n: f for n, f in out.items() if f}
    import os
    tmp = cache.with_name(cache.name + ".%d" % os.getpid())  # (slice_all's workers may all get here at once)
    tmp.write_text(json.dumps(out))
    os.replace(tmp, cache)
    return out


if __name__ == "__main__":
    for n in sys.argv[1:] or sorted(_load()["cls_of"]):
        f = flags(n)
        if f or len(sys.argv) > 1:
            print(n, _load()["cls_of"].get(n), f)
