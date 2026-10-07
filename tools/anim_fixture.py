"""Reference poses for engine/test/test_anim.c: Mario's draw matrices from tools/skin.py (the evaluator checked with
renders, docs/renders/phase2-mario-poses.png) for the clips the player uses, at several frames.

usage: python tools/anim_fixture.py data/fs/files engine/test/fixtures/anim_expected.bin
file: u32 count, count x {char[32] clip, f32 frame, u32 ndrw, ndrw x f32[16] (column-major)}
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from budget import rarc_files
import skin
import slice as SL
import tex

CLIPS = ("wait", "walk", "run", "jump", "jump2", "jumproll", "land", "jump2land", "jumprollland", "fall", "jumpbroad",
         "jumpbroadland", "jumpback", "jumpbackland", "spin", "spinground", "walljump", "wallslide", "squatwait")


def main(fs, out):
    fs = Path(fs) / "ObjectData"
    b = next(d for n, d in rarc_files((fs / "Mario.arc").read_bytes()) if n.endswith(".bdl"))
    bl = tex.blocks(b)
    js, ents = skin.joints(b, bl), skin.draw_entries(b, bl)
    clips = {n.rsplit("/", 1)[-1][:-4].lower(): SL.anim_clip(d) for n, d in rarc_files((fs / "MarioAnime.arc").read_bytes())
             if n.endswith(".bck")}
    recs = []
    for c in CLIPS:
        fmax = struct.unpack_from("<H", clips[c], 0)[0]
        for f in sorted({0.0, 1.0, fmax * 0.25, fmax * 0.5 + 0.5, fmax * 0.75, float(fmax)}):
            w = skin.world(js, [skin.local(*p) for p in skin.clip_pose(clips[c], f, js)])
            dm = skin.draw_mtx(ents, w)
            rec = SL.name32(c) + struct.pack("<fI", f, len(dm))
            for m in dm:
                rec += struct.pack("<16f", *m.T.reshape(-1))
            recs.append(rec)
    Path(out).write_bytes(struct.pack("<I", len(recs)) + b"".join(recs))
    print(len(recs), "poses ->", out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
