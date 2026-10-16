"""One gameplay timeline for both games (the Wii-vs-PS2 comparison of a level):

  python tools/replay.py dtm  play.json out.dtm    -> Dolphin movie: boot, title, file 1, the Gecko warp
                                                      (tools/dolphin/RMGE01.ini), then the gameplay from control
  python tools/replay.py bin  play.json out/replay.bin -> the PS2 port's `replay` build (engine/src/pad.c)

play.json: [{"g": frame, ...state}, ...], g counted at 60 Hz from the first frame Mario is free (both games);
state keys persist until changed: stick [x, y] (-1..1, y up), a, b, z (bools), shake (bool: held = shaking),
dpad [..]. Wii -> PS2 mapping as the port's controls: A CROSS, B L1, Z R1, shake SQUARE, D-pad D-pad.
"""
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "dolphin"))
import dtm  # noqa: E402

# the movie's menu part (calibrated 2026-10-15 in Dolphin 2609a: tools/dolphin/play.ps1 runs), timeline frames
def _at(sx, sy):  # screen place (0..1) -> dtm.pointer arguments (cursor = 0.5 + 0.88 px, 0.32 - 0.6 py)
    return [(sx - 0.5) / 0.88, (0.32 - sy) / 0.6]


MENU = [{"frame": 0}, {"frame": 1800, "buttons": ["A", "B"]}, {"frame": 1820, "buttons": []},
        {"frame": 1900, "point": _at(0.375, 0.62)}, {"frame": 2400, "buttons": ["A"]}, {"frame": 2410, "buttons": []},
        {"frame": 2500, "point": _at(0.79, 0.88)}, {"frame": 2700, "buttons": ["A"]}, {"frame": 2710, "buttons": []},
        {"frame": 2800, "point": None}]
CONTROL_T = 4155  # timeline frame Mario is first free in EggStarGalaxy 1: video 4050 (he answers the stick), video = t - 106 there (pointer probe)

PS2 = {"a": 0x4000, "b": 0x0400, "z": 0x0800, "shake": 0x8000}  # libpad: CROSS, L1, R1, SQUARE
PS2_DPAD = {"UP": 0x0010, "RIGHT": 0x0020, "DOWN": 0x0040, "LEFT": 0x0080}


def states(play):
    """per g frame: the full state"""
    tl = sorted(play, key=lambda k: k["g"])
    st = {"stick": [0, 0], "a": False, "b": False, "z": False, "shake": False, "dpad": []}
    out, i = [], 0
    for g in range(tl[-1]["g"] + 1):
        while i < len(tl) and tl[i]["g"] <= g:
            st.update({k: v for k, v in tl[i].items() if k != "g"})
            i += 1
        out.append(dict(st))
    return out


def to_dtm(play):
    tl = list(MENU)
    for e in sorted(play, key=lambda k: k["g"]):
        m = {"frame": CONTROL_T + e["g"]}
        for k in ("stick", "z", "shake", "dpad"):
            if k in e:
                m[k] = e[k]
        tl.append(m)
    # buttons: rebuilt from the per-frame states (A / B live in one list in dtm.py)
    prev = None
    for g, s in enumerate(states(play)):
        b = [k for k, on in (("A", s["a"]), ("B", s["b"])) if on]
        if b != prev:
            tl.append({"frame": CONTROL_T + g, "buttons": b})
            prev = b
    tl.append({"frame": CONTROL_T + len(states(play)) + 120})
    return dtm.build(tl)


def to_bin(play):
    out = bytearray()
    for s in states(play):
        b = sum(v for k, v in PS2.items() if s[k]) | sum(PS2_DPAD[d] for d in s["dpad"])
        q = lambda v: max(-127, min(127, round(v * 127)))  # noqa: E731
        out += struct.pack("<Hbbbb", b, q(s["stick"][0]), q(s["stick"][1]), 0, 0)
    return bytes(out)


def _selfcheck():
    play = [{"g": 0, "stick": [0, 1]}, {"g": 10, "a": True}, {"g": 12, "a": False}, {"g": 20, "shake": True},
            {"g": 26, "shake": False, "stick": [0, 0]}]
    b = to_bin(play)
    assert len(b) == 6 * 27
    assert struct.unpack_from("<Hbb", b, 6 * 10) == (0x4000, 0, 127)
    assert struct.unpack_from("<H", b, 6 * 20)[0] == 0x8000 and struct.unpack_from("<bb", b, 6 * 26 + 2) == (0, 0)
    m = to_dtm(play)
    assert m[:4] == b"DTM\x1a"
    print("ok")


if __name__ == "__main__":
    if len(sys.argv) == 4:
        play = json.load(open(sys.argv[2]))
        Path(sys.argv[3]).write_bytes(to_dtm(play) if sys.argv[1] == "dtm" else to_bin(play))
    else:
        _selfcheck()
