"""Sequenced BGMs (AudioRes/Seqs/JaiSeq.arc BMS, the ones with no stream: the power-ups' themes, the jingles, the
scenario select...) rendered offline through tools/sound.py's JAudio2 sequencer (the same rules as the engine's SE
sequencer) into the music streams' format (tools/music.py .pcm: 'SMGM', rate, channels, loop start byte, PCM16 LE
stereo), so the engine plays them as it plays the streamed ones.

usage: python tools/bgmseq.py data/fs/files out/music [BGM_NAME...]     (no names: every one in SEQ_BGMS)
       python tools/bgmseq.py --selftest

A BGM's sound table item (BST section 1): {u8 priority, u8 volume, u16 JaiSeq index, u16 stream index, u16 flags};
the JaiSeq files in name order (JAUSeqDataMgr: the archive's index). Each track's pan (param 3, 0.5 the middle)
adds to its parents' (JASTrack::updateTrack's pan composition) and places its notes (equal power). The loop: the
root track's first jump back to a place it has been (the intro then the loop to its end); a sequence whose tracks
all end is a one-shot (loop field 0xFFFFFFFF: played once). All are scaled by one gain (the loudest peak at 0.9),
so their table volumes stay as they are to each other.
ponytail: the DSP's interpolation, vibrato / tremolo, FIR / IIR and the effect buses are the reference renderer's
(not reproduced); no game callbacks (syncCPU 0xFFFF)."""
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sound as S  # noqa: E402
from budget import rarc_files, yaz0  # noqa: E402

# the sequenced BGMs the engine starts by name (bgm_start / bgm_sub / the jingles): those with no stream
SEQ_BGMS = ("BGM_FIRE_A", "BGM_FIRE_B", "BGM_ICE_A", "BGM_ICE_B", "BGM_MUTEKI_A", "BGM_MUTEKI_B", "BGM_FLYING_A",
            "BGM_FLYING_B", "BGM_FIRST_MORPH_A", "BGM_FIRST_MORPH_B", "BGM_IN_BUBBLE", "BGM_TAMAKORO", "BGM_TAMAKORO_2",
            "BGM_KOOPA_JR_APPEAR", "BGM_PINCH_1", "BGM_PINCH_2", "BGM_PINCH_3", "BGM_SENARIO_SEL", "BGM_SENARIO_SEL_2",
            "BGM_SENARIO_SEL_3", "BGM_CLEAR", "BGM_MISS", "BGM_GAMEOVER", "BGM_MINIGAME_START", "BGM_RACE_WIN",
            "BGM_RACE_LOSE", "BGM_START_DEMO", "BGM_STAR_POW_GET", "BGM_ASTRO_RETURN", "BGM_NEW_GALAXY",
            "BGM_KOOPA_APPEAR", "BGM_RED_COMET_APPEAR", "BGM_GRAND_STAR_GET", "BGM_MEET_TICO_ZOOM_OUT",
            "BGM_APPEAR_PLANET", "BGM_APPEAR_HD_TOWER", "BGM_HIGH_SCORE", "BGM_KOOPA_GRAND_STAR_1",
            "BGM_KOOPA_GRAND_STAR_3", "BGM_GRAND_STAR_GET_2", "BGM_JINGLE_LIGHT_1", "BGM_KOOPA_VS3_SUN",
            "BGM_GALAXY_COMPLETE", "BGM_LIBRARY", "BGM_ASTRO_DOME_LOFT")
MAX_SECONDS = 600


def bgm_item(baa, name):
    """-> (volume 0..1, JaiSeq index, stream index, flags) of BGM `name` (1, group, index)"""
    U32, d, b = S.U32, baa.d, baa.bst
    sid = baa.ids[name]
    root = b + U32(d, b + 12)
    sec = b + U32(d, root + 4 + 4 * sid[0])
    go = b + U32(d, sec + 4 + 4 * sid[1])
    it = b + (U32(d, go + 8 + 4 * sid[2]) & 0xFFFFFF)
    return d[it + 1] / 255, S.U16(d, it + 2), S.U16(d, it + 4), S.U16(d, it + 6)


def jaiseq(fs):
    return [yaz0(b) for _, b in sorted(rarc_files((Path(fs) / "AudioRes/Seqs/JaiSeq.arc").read_bytes()),
                                       key=lambda nb: nb[0].rsplit("/", 1)[-1])]


class LoopTrack(S.Track):
    """S.Track keeping the places the root went (by sub-frame) and noting its first jump back to one"""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.seen, self.loop, self.now = {}, None, 0

    def step(self, chans):
        if self.parent is None:
            self.seen.setdefault(self.pc, self.now)
        before = self.pc
        super().step(chans)
        if self.parent is None and self.loop is None and self.pc < before and self.pc in self.seen:
            self.loop = (self.seen[self.pc], self.now)


def pan_of(t):
    p = 0.5
    while t is not None:
        p += t.params[3] - 0.5
        t = t.parent
    return min(1.0, max(0.0, p))


def render(baa, seq, vol):
    """-> (float32 stereo PCM at the DAC rate, loop start sample or None)"""
    root = LoopTrack(seq, baa, 0, se_vol=vol)
    chans, out, acc, n = [], [], 1.0, 0
    for n in range(int(MAX_SECONDS * S.DAC / S.SUB)):
        root.now = n
        while acc >= 1.0:
            acc -= 1.0
            root.tick(chans)
        acc += root.interval()
        buf = np.zeros((S.SUB, 2), np.float32)
        tmp = np.zeros(S.SUB, np.float32)
        for c in chans:
            if c.alive:
                tmp[:] = 0
                c.render(tmp)
                p = pan_of(c.track)
                buf[:, 0] += tmp * np.sin((1.0 - p) * np.pi / 2)
                buf[:, 1] += tmp * np.sin(p * np.pi / 2)
        chans = [c for c in chans if c.alive]
        out.append(buf)
        if root.loop and n >= root.loop[1]:
            return np.concatenate(out), root.loop[0] * S.SUB
        if root.done and not chans and all(ch.done for ch in S.all_tracks(root)):
            return np.concatenate(out), None
    return np.concatenate(out), None


def main(fs, out, names):
    baa = S.Baa(fs)
    seqs = jaiseq(fs)
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    done = []
    for name in names or SEQ_BGMS:
        vol, idx, _, _ = bgm_item(baa, name)
        pcm, loop = render(baa, seqs[idx], vol)
        done.append((name, pcm, loop))
        print(f"{name}: {len(pcm) / S.DAC:.1f} s, " + (f"loop at {loop / S.DAC:.1f} s" if loop is not None else "once"))
    gain = 0.9 / max(1e-6, max(float(np.abs(p).max()) for _, p, _ in done))
    for name, pcm, loop in done:
        # resampled from the DAC rate to 32000 (the streams' rate)
        n = int(len(pcm) * 32000 / S.DAC)
        x = np.linspace(0, len(pcm) - 1, n)
        st = np.stack([np.interp(x, np.arange(len(pcm)), pcm[:, c]) for c in (0, 1)], 1) * gain
        data = (np.clip(st, -1, 1) * 32767).astype("<i2").tobytes()
        lp = 0xFFFFFFFF if loop is None else int(loop * 32000 / S.DAC) * 4
        (out / f"SEQ_{name[4:].lower()}.pcm").write_bytes(struct.pack("<4sIII", b"SMGM", 32000, 2, lp) + data)
    print(f"gain {gain:.3f}")


def selftest():
    fs = Path(__file__).resolve().parent.parent / "data/fs/files"
    baa = S.Baa(fs)
    vol, idx, _, flags = bgm_item(baa, "BGM_FIRE_A")
    assert idx == 23, idx  # smg_ev_fire_a_cnv
    seqs = jaiseq(fs)
    pcm, loop = render(baa, seqs[idx], vol)
    assert loop is not None and 0 <= loop < len(pcm), (loop, len(pcm))
    assert np.abs(pcm).max() > 0.001
    print("bgmseq selftest ok", f"{len(pcm) / S.DAC:.1f} s loop at {loop / S.DAC:.1f} s")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--selftest"]:
        selftest()
    else:
        main(sys.argv[1], sys.argv[2], sys.argv[3:])
