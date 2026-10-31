"""Sound effects offline: SMR.szs (the BAA) -> an SE played through Petari's JAudio2 rules -> PCM / WAV.

A reference renderer for the port's sequencer (same rules, in C, at run time) and a way to hear the data:
  python tools/sound.py data/fs/files SE_SY_COIN [more names...] [--out dir]   -> <dir>/<name>.wav (32 kHz mono)
  python tools/sound.py --selftest

Data (big-endian; layouts from Petari JASWSParser.hpp, JASBNKParser.cpp Ver1, JAUSoundTable.hpp, JAUSeqCollection.hpp,
JAUAudioArcInterpreter.cpp):
  BAA 'AA_<' commands: 'ws  ' {wsys no, offset, load mask}, 'bnk ' {wsys no, offset} (bank number = IBNK u32 @+8),
  'bsc ' / 'bst ' / 'bstn' {start, end}, ... '>_AA'.
  BSTN: root @+0xC {sections, offsets}; section {groups, ?, group offsets}; group {items, category name, name offsets}.
  BST: same tree, section {groups, offsets}, group {items, ?, words: type << 24 | item offset}; SE item {priority,
  volume (/ 255), audible sw, sound sw}.
  BSC 'SC' {groups, size, table offsets}; table {count, sequence offsets} (offsets from the BSC start, which is also
  the sequence base: jumps and opens are BSC offsets).
  IBNK v1: chunks from +0x20 (ENVT point pool, OSCT 'Osci' records, LIST program -> 'Inst' / 'Perc' offsets).
  WSYS: WINF groups {aw name, waves {format, base key, rate, aw offset / length, loop, loop start / end, samples}},
  WBCT -> SCNE -> C-DF {group, wave id} parallel to the waves; AW files in AudioRes/Waves.
Playback (Petari JASSeqParser / JASTrack / JASChannel / JASOscillator / JASBasicInst / JASDrumSet): 80-sample
sub-frames at the DAC rate (32028.47 Hz); the sequence ticks timebase x tempo / 60 a second (48 x 120 by default),
envelopes advance 48000 / dac x rate a sub-frame, a note's step is waveRate / dac x inst x region pitch x
2^((key - baseKey + sweep) / 12 + track pitch x bendSense / 3) x pitch oscillators, its volume (vel / 127)^2 x inst x
region x oscillators x track volume^2 (parents multiplied) x the SE's table volume.
ponytail: linear interpolation (the DSP's 4-tap filter not reproduced); no vibrato / tremolo, FIR / IIR, buses,
interrupts or game callbacks (syncCPU reads 0xFFFF); Rand effects take their base value; mono (pan ignored).
"""
import os
import re
import struct
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from budget import yaz0  # noqa: E402
from bms import CMD  # noqa: E402  (opcode -> (numArgs, argWidths), Petari sCmdInfo)

DAC = 32000 * 1.0008897
SUB = 80
U32 = lambda b, o: struct.unpack_from(">I", b, o)[0]
U16 = lambda b, o: struct.unpack_from(">H", b, o)[0]
F32 = lambda b, o: struct.unpack_from(">f", b, o)[0]
AFC_COEF = ((0, 0), (2048, 0), (0, 2048), (1024, 1024), (4096, -2048), (3584, -1536), (3072, -1024), (4608, -2560),
            (4200, -2248), (4800, -2300), (5120, -3072), (2048, -2048), (1024, -1024), (-1024, 1024), (-1024, 0),
            (-2048, 0))
CURVES = None  # JASOscillator curve tables, filled below


def _curves():
    """JASOscillator's table_list {Linear, Square, SqRoot, SampleCell}, read from Petari's JASOscillator.cpp"""
    src = (Path(__file__).resolve().parent.parent / "vendor/Petari/src/JSystem/JAudio2/JASOscillator.cpp").read_text()
    out = []
    for n in ("Linear", "Square", "SqRoot", "SampleCell"):
        body = re.search(r"sCurveTable" + n + r"\[17\] = \{([^}]*)\}", src).group(1)
        out.append([float(x.strip().rstrip("f")) for x in body.split(",") if x.strip()])
        assert len(out[-1]) == 17, n
    return out


class Baa:
    """The parsed BAA: names, SE table, BSC, banks and wave systems."""

    def __init__(self, fs):
        self.fs = Path(fs)
        d = self.d = yaz0((self.fs / "AudioRes/SMR.szs").read_bytes())
        self.wsys, self.banks, o = {}, {}, 4
        args = {b"ws  ": 3, b"bnk ": 2, b"bl_<": 2, b">_bl": 0, b"bsc ": 2, b"bst ": 2, b"bstn": 2, b"bms ": 3,
                b"bmsa": 1, b"vbnk": 2, b"dsqb": 1, b"bsft": 1, b"sect": 1}
        while d[o:o + 4] != b">_AA":
            tag = d[o:o + 4]
            a = [U32(d, o + 4 + 4 * i) for i in range(args[tag])]
            if tag == b"ws  ":
                self.wsys[a[0]] = a[1]
            elif tag == b"bnk ":
                self.banks[U32(d, a[1] + 8)] = (a[1], a[0])
            elif tag in (b"bsc ", b"bst ", b"bstn"):
                setattr(self, tag.decode().strip(), a[0])
            o += 4 + 4 * len(a)
        self.names = self._names()
        self.ids = {v: k for k, v in self.names.items()}
        self._insts, self._waves, self._aw = {}, {}, {}

    def _names(self):
        d, b = self.d, self.bstn
        root, out = b + U32(d, b + 12), {}
        for s in range(U32(d, root)):
            so = U32(d, root + 4 + 4 * s)
            if not so:
                continue
            for g in range(U32(d, b + so)):
                go = b + U32(d, b + so + 8 + 4 * g)
                for i in range(U32(d, go)):
                    out[(s, g, i)] = d[b + U32(d, go + 8 + 4 * i):].split(b"\0")[0].decode()
        return out

    def se_item(self, sid):
        """-> (priority, volume 0..1) of SE (0, g, i) (JAUSoundTableSe; JAUStdSoundInfo: volume / 255)"""
        d, b = self.d, self.bst
        root = b + U32(d, b + 12)
        sec = b + U32(d, root + 4 + 4 * sid[0])
        go = b + U32(d, sec + 4 + 4 * sid[1])
        it = b + (U32(d, go + 8 + 4 * sid[2]) & 0xFFFFFF)
        return d[it], d[it + 1] / 255

    def se_seq(self, sid):
        """-> offset of SE (0, g, i)'s sequence in the BSC (JAUSeqCollection::getSeqData(group, index))"""
        d, b = self.d, self.bsc
        t = b + U32(d, b + 8 + 4 * sid[1])
        return U32(d, t + 4 + 4 * sid[2])

    def wave_archives(self):
        """-> {(wsys_no, wave id): its archive's index in the WSYS (what AudSceneMgr loads: loadWaveArc)}"""
        d, out = self.d, {}
        for ws, w in self.wsys.items():
            winf, wbct = w + U32(d, w + 0x10), w + U32(d, w + 0x14)
            for gi in range(U32(d, winf + 4)):
                ga = w + U32(d, winf + 8 + 4 * gi)
                cdf = w + U32(d, w + U32(d, wbct + 12 + 4 * gi) + 12)
                for j in range(U32(d, ga + 0x70)):
                    out[(ws, U16(d, w + U32(d, cdf + 8 + 4 * j) + 2))] = gi
        return out

    def wave(self, wsys_no, wid):
        """-> (float32 samples, rate, base key, loop (start, end) or None) of wave id in wave system wsys_no"""
        key = (wsys_no, wid)
        if key in self._waves:
            return self._waves[key]
        d, w = self.d, self.wsys[wsys_no]
        winf, wbct = w + U32(d, w + 0x10), w + U32(d, w + 0x14)
        for gi in range(U32(d, winf + 4)):
            ga = w + U32(d, winf + 8 + 4 * gi)
            scne = w + U32(d, wbct + 12 + 4 * gi)
            cdf = w + U32(d, scne + 12)
            for j in range(U32(d, ga + 0x70)):
                if U16(d, w + U32(d, cdf + 8 + 4 * j) + 2) != wid:
                    continue
                wo = w + U32(d, ga + 0x74 + 4 * j)
                name = d[ga:ga + 0x70].split(b"\0")[0].decode()
                fmt, base = d[wo + 1], d[wo + 2]
                rate, aoff, alen, lflag, ls, le, n = struct.unpack_from(">f6I", d, wo + 4)
                if name not in self._aw:
                    self._aw[name] = (self.fs / "AudioRes/Waves" / name).read_bytes()
                pcm = decode(self._aw[name][aoff:aoff + alen], fmt, n)
                self._waves[key] = (pcm, rate, base, (ls, le) if lflag else None)
                return self._waves[key]
        raise KeyError(f"wave {wid} not in wsys {wsys_no}")

    def inst(self, bank, prog):
        """-> instrument dict of IBNK `bank` program `prog` (None: none), with its wave system"""
        key = (bank, prog)
        if key not in self._insts:
            self._insts[key] = self._inst(bank, prog)
        return self._insts[key]

    def _inst(self, bank, prog):
        if bank not in self.banks:
            return None
        d = self.d
        ib, wsys_no = self.banks[bank]
        size, chunks, o = U32(d, ib + 4), {}, ib + 0x20
        while o < ib + size:
            chunks[d[o:o + 4]] = o
            o = (o + 0xB + U32(d, o + 4)) & ~3
        lst = chunks[b"LIST"]
        if prog >= U32(d, lst + 8):
            return None
        ro = U32(d, lst + 12 + 4 * prog)
        if not ro:
            return None
        envt = chunks[b"ENVT"] + 8
        osct = chunks[b"OSCT"]

        def osc(i):
            r = osct + 12 + 0x1C * i
            return dict(target=d[r + 4], rate=F32(d, r + 8), table=points(d, envt + U32(d, r + 12)),
                        rel=points(d, envt + U32(d, r + 16)), scale=F32(d, r + 20), vertex=F32(d, r + 24))

        def effects(o):
            out = []
            for k in range(U32(d, o)):
                e = ib + U32(d, o + 4 + 4 * k)
                if d[e:e + 4] == b"Rand":
                    out.append(("rand", d[e + 4], F32(d, e + 8), F32(d, e + 12)))
                elif d[e:e + 4] == b"Sens":
                    out.append(("sens", d[e + 4], d[e + 5], d[e + 6], F32(d, e + 8), F32(d, e + 12)))
            return out, o + 4 + 4 * U32(d, o)

        def velos(o):
            out = []
            for k in range(U32(d, o)):
                v = o + 4 + 16 * k
                out.append((d[v], U32(d, v + 4) & 0xFFFF, F32(d, v + 8), F32(d, v + 12)))
            return out, o + 4 + 16 * U32(d, o)

        r = ib + ro
        if d[r:r + 4] == b"Inst":
            n = U32(d, r + 4)
            oscs = [osc(U32(d, r + 8 + 4 * k)) for k in range(n)]
            eff, o = effects(r + 8 + 4 * n)
            keys = []
            for _ in range(U32(d, o)):
                hk = d[o + 4]
                vs, o2 = velos(o + 8)
                keys.append((hk, vs))
                o = o2 - 4
            o += 4
            return dict(kind="inst", wsys=wsys_no, oscs=oscs, effects=eff, keys=keys, volume=F32(d, o),
                        pitch=F32(d, o + 4))
        if d[r:r + 4] == b"Perc":
            perc = {}
            for k in range(U32(d, r + 4)):
                po = U32(d, r + 8 + 4 * k)
                if not po:
                    continue
                p = ib + po
                eff, o = effects(p + 0x10)
                vs, _ = velos(o)
                perc[k] = dict(volume=F32(d, p + 4), pitch=F32(d, p + 8), pan=d[p + 12] / 127, release=U16(d, p + 14),
                               effects=eff, velos=vs)
            return dict(kind="perc", wsys=wsys_no, perc=perc)
        return None


def points(d, o):
    """an envelope table (JASOscillator::Point {mode, time, value}) from o, through its stop / hold / loop point"""
    out = []
    for k in range(64):
        m, t, v = struct.unpack_from(">3h", d, o + 6 * k)
        out.append((m, t, v))
        if m in (0xD, 0xE, 0xF):
            break
    return out


def decode(b, fmt, n):
    """AW wave data -> float32 PCM (-1..1): AFC 9-byte / 5-byte frames (vgmstream afc_decoder), PCM8, PCM16 BE"""
    if fmt == 3:
        return np.frombuffer(b[:2 * n], ">i2").astype(np.float32) / 32768
    if fmt == 2:
        return np.frombuffer(b[:n], np.int8).astype(np.float32) / 128
    fb = 9 if fmt == 0 else 5
    out = np.zeros(((len(b) // fb) * 16,), np.int32)
    h1 = h2 = 0
    k = 0
    for f in range(len(b) // fb):
        fr = b[f * fb:(f + 1) * fb]
        c1, c2 = AFC_COEF[fr[0] & 0xF]
        if fmt == 0:
            scale = 1 << (fr[0] >> 4)
            nib = [(fr[1 + i // 2] >> (4 if i % 2 == 0 else 0)) & 0xF for i in range(16)]
            nib = [x - 16 if x >= 8 else x for x in nib]
            vals = [(x * scale) << 11 for x in nib]
        else:
            scale = 8192 << (fr[0] >> 4)
            nib = [(fr[1 + i // 4] >> (6 - 2 * (i % 4))) & 3 for i in range(16)]
            vals = [(x - 4 if x >= 2 else x) * scale for x in nib]
        for v in vals:
            s = (v + c1 * h1 + c2 * h2) >> 11
            s = -32768 if s < -32768 else 32767 if s > 32767 else s
            out[k] = s
            k += 1
            h2, h1 = h1, s
    return out[:n].astype(np.float32) / 32768


class Osc:
    """JASOscillator: attack table, hold, release table or direct release; value x scale + vertex"""

    def __init__(self, data):
        self.data, self.t, self.v, self.v0, self.v1, self.i, self.dr = data, 0.0, 0.0, 0.0, 0.0, 0, 0
        self.mode, self.state = 0, 0
        if data is None:
            return
        if not data["table"]:
            self.state, self.v = 2, 1.0
            return
        self.v1, self.mode, self.state = data["table"][0][2] / 32768, data["table"][0][0], 1

    def valid(self):
        return self.data is not None

    def inc(self, inc):
        if self.state in (0, 2):
            return
        self.t += inc * self.data["rate"]
        self.update()

    def cur(self, ln):
        lin, sq, sqr, cell = CURVES
        tab = (lin, sq, sqr, cell)[self.mode & 3]
        x = 16.0 * (self.t / ln) if ln else 16.0
        x = min(x, 16.0)
        i = min(int(x), 15)
        f = x - i
        c = (1 - f) * tab[i] + f * tab[i + 1]
        self.v = self.v0 * c + self.v1 * (1 - c)

    def update(self):
        if self.state == 4:
            x = self.dr & 0x3FFF
            if self.t < x:
                self.cur(x)
            else:
                self.v, self.state = self.v1, 0
            return
        if self.state == 2:
            return
        t = self.data["table"] if self.state == 1 else self.data["rel"]
        if not t:
            self.state = 2
            return
        while self.t >= t[self.i][1]:
            self.t -= t[self.i][1]
            self.v = self.v1
            self.i += 1
            self.v0 = self.v
            if self.i >= len(t):
                self.state = 0
                return
            m, _, val = t[self.i]
            if m == 0xF:
                self.state = 0
                return
            if m == 0xE:
                self.state = 2
                return
            if m == 0xD:
                self.i = val
            else:
                self.mode, self.v1 = m, val / 32768
        self.cur(t[self.i][1])

    def release(self):
        d = self.data
        if not d["rel"] and not self.dr:
            self.state = 0
            return
        if self.dr:
            self.t, self.v0, self.v1, self.i = 0.0, self.v, 0.0, 0
            self.mode, self.state = (self.dr >> 14) & 3, 4
            self.update()
            return
        if d["table"] is not d["rel"]:
            self.t, self.v0, self.v1, self.i = 0.0, self.v, d["rel"][0][2] / 32768, 0
            self.mode = d["rel"][0][0]
        self.state = 3
        self.update()

    def value(self):
        return self.v * self.data["scale"] + self.data["vertex"]


class Channel:
    def __init__(self, track, pcm, step, key, vel, vol, oscs, dr, timer):
        self.track, self.pcm, self.step, self.key, self.vel, self.vol = track, pcm[0], step, key, vel, vol
        self.base = pcm[2] if key is not None else None  # setKey(key - baseKey); drums keep key 0
        self.key = key - pcm[2] if key is not None else 0
        self.loop = pcm[3]
        self.oscs = [Osc(o) for o in oscs] + [Osc(None)] * (2 - len(oscs))
        for o in self.oscs:
            o.dr = dr
        self.pos, self.timer, self.sweep, self.sweep_to, self.sweep_n = 0.0, timer, 0.0, 0.0, 0
        self.alive, self.released = True, False

    def release(self, dr=0):
        if self.released or not self.alive:
            return
        self.released = True
        valid = False
        for o in self.oscs:
            if o.valid():
                if dr:
                    o.dr = dr
                o.release()
                valid = True
        if not valid:
            self.alive = False

    def render(self, out):
        """one sub-frame (JASChannel::updateDSPChannel) mixed into out"""
        if self.timer:
            self.timer -= 1
            if self.timer == 0:
                self.release()
        vol, pitch = 1.0, 1.0
        for k, o in enumerate(self.oscs):
            if not o.valid():
                continue
            o.inc(48000.0 / DAC)
            v = o.value()
            if o.data["target"] == 0:
                vol *= v
            elif o.data["target"] == 1:
                pitch *= v
            if k == 0 and o.state == 0:
                self.alive = False
                return
        tp = self.track.mix
        step = self.step * pitch * tp["pitch"] * 2.0 ** (tp["oct"] + (self.key + self.sweep) / 12.0)
        if self.sweep_n:
            self.sweep += (self.sweep_to - self.sweep) / self.sweep_n
            self.sweep_n -= 1
        amp = max(0.0, (self.vel / 127.0) ** 2 * self.vol * vol * tp["vol"])
        pos = self.pos + step * np.arange(SUB)
        pcm = self.pcm
        if self.loop:
            ls, le = self.loop
            le = min(le, len(pcm))
            over = pos >= le
            if over.any() and le > ls:
                pos = np.where(over, ls + (pos - ls) % (le - ls), pos)
        else:
            if pos[0] >= len(pcm) - 1:
                self.alive = False
                return
        i = np.minimum(pos.astype(np.int64), len(pcm) - 2)
        f = pos - i
        s = pcm[i] * (1 - f) + pcm[i + 1] * f
        if not self.loop:
            s = np.where(pos < len(pcm) - 1, s, 0)
        out += amp * s
        self.pos = pos[-1] + step
        if self.loop and self.pos >= self.loop[1] > self.loop[0]:
            self.pos = self.loop[0] + (self.pos - self.loop[0]) % (self.loop[1] - self.loop[0])


class Track:
    def __init__(self, seq, baa, pc, parent=None, se_vol=1.0):
        self.seq, self.baa, self.pc, self.parent, self.se_vol = seq, baa, pc, parent, se_vol
        self.stack, self.regs, self.ports = [], [0] * 14, [0] * 16
        self.params = [1.0, 0.0, 0.0, 0.5, 0.0, 0.0]
        self.ramps = [[0.0, 0] for _ in range(6)]
        p = parent
        self.transpose, self.bend, self.gate_rate = 0, 12, 100
        self.bank, self.prog = (p.bank, p.prog) if p else (0, 0xF0)
        self.timebase, self.tempo = (p.timebase, p.tempo) if p else (48, 120)
        self.wait, self.done, self.wait_note = 0, False, False
        self.children, self.notes, self.last_key, self.ready, self.dr = {}, {}, 60, False, 0
        self.osc_over = [None, None]
        self.mix = dict(vol=1.0, pitch=1.0, oct=0.0)

    def root(self):
        return self if not self.parent else self.parent.root()

    def interval(self):
        """sequence ticks per sub-frame (JASTrack::updateTempo)"""
        r = self.root()
        return 4.0 / 3.0 * r.timebase * r.tempo / DAC * SUB / 80

    # byte reading
    def u8(self):
        v = self.seq[self.pc]
        self.pc += 1
        return v

    def u16(self):
        v = U16(self.seq, self.pc)
        self.pc += 2
        return v

    def u24(self):
        v = U32(self.seq, self.pc - 1) & 0xFFFFFF
        self.pc += 3
        return v

    def vlq(self):
        v = 0
        for _ in range(4):
            b = self.u8()
            v = (v << 7) | (b & 0x7F)
            if not b & 0x80:
                return v
        return 0

    def reg(self, r):
        if r < 14:
            return self.regs[r]
        if r == 32:
            return (self.regs[0] << 8) | self.regs[1]
        if r == 33:
            return (self.regs[4] << 16) | self.regs[5]
        if 0x40 <= r < 0x50:
            return self.ports[r - 0x40]
        return {0x60: sum(1 << k for k, c in self.children.items() if not c.done), 0x62: self.timebase,
                0x63: self.transpose, 0x64: self.bend, 0x65: self.gate_rate, 0x67: self.bank, 0x68: self.prog,
                0x6D: self.dr}.get(r, 0)

    def wreg(self, r, v):
        v &= 0xFFFF
        if r < 14:
            self.regs[r] = v & 0xFF if r < 3 else v
        elif r == 33:
            self.regs[4], self.regs[5] = (v >> 16) & 0xFFFF, v & 0xFFFF
        elif 0x40 <= r < 0x50:
            self.ports[r - 0x40] = v
        elif r == 0x62:
            self.timebase = v
        elif r == 0x63:
            self.transpose = v - 0x10000 if v & 0x8000 else v
        elif r == 0x64:
            self.bend = v
        elif r == 0x65:
            self.gate_rate = v
        elif r == 0x67:
            self.bank = v
        elif r == 0x68:
            self.prog = v
        elif r == 0x6D:
            self.dr = v
        self.regs[3] = v

    def transpose_total(self):
        return self.transpose + (self.parent.transpose_total() if self.parent else 0)

    def cond(self, c):
        f = self.regs[3]
        return (c == 0 or (c == 1 and f == 0) or (c == 2 and f != 0) or (c == 3 and f == 1) or
                (c == 4 and f >= 0x8000) or (c == 5 and f < 0x8000))

    def start(self, key, vel, timer):
        """JASTrack::channelStart: JASBank::noteOn through the current bank / program"""
        inst = self.baa.inst(self.bank, self.prog)
        if inst is None:
            return None
        vol = pitch = 1.0
        if inst["kind"] == "inst":
            vol, pitch, oscs, dr = inst["volume"], inst["pitch"], inst["oscs"], 0
            vol, pitch = effect(inst["effects"], key, vel, vol, pitch)
            km = next((k for k in inst["keys"] if key <= k[0]), None)
            if km is None:
                return None
            vm = next((v for v in km[1] if vel <= v[0]), None)
        else:
            p = inst["perc"].get(key)
            if p is None:
                return None
            vol, pitch, dr = p["volume"], p["pitch"], p["release"]
            vol, pitch = effect(p["effects"], key, vel, vol, pitch)
            oscs = [dict(target=0, rate=1.0, table=[], rel=[], scale=1.0, vertex=0.0)]
            vm = next((v for v in p["velos"] if vel <= v[0]), None)
        if vm is None:
            return None
        pcm = self.baa.wave(inst["wsys"], vm[1])
        oscs = [self.osc_over[k] if k < 2 and self.osc_over[k] else o for k, o in enumerate(oscs)]
        if self.dr:
            dr = self.dr
        step = pitch * vm[3] * pcm[1] / DAC
        return Channel(self, pcm, step, key if inst["kind"] == "inst" else None, vel, vol * vm[2], oscs, dr, timer)

    def note_off(self, nid, dr=0):
        c = self.notes.pop(nid, None)
        if c:
            c.release(dr)

    def channels(self):
        out = [c for c in self.notes.values()]
        for ch in self.children.values():
            out += ch.channels()
        return out

    def tick(self, chans):
        """JASSeqCtrl::tickProc then the children, then the timed parameters"""
        if not self.done:
            if self.wait > 0:
                self.wait -= 1
            while not self.done and self.wait == 0:
                if self.wait_note:
                    c = self.notes.get(0)
                    if c and c.alive:
                        break
                    self.wait_note = False
                self.step(chans)
        for c in list(self.children.values()):
            c.tick(chans)
        for k in range(6):
            tgt, n = self.ramps[k]
            if n:
                self.params[k] += (tgt - self.params[k]) / n
                self.ramps[k][1] = n - 1
        self.update_mix()

    def update_mix(self):
        """JASTrack::updateTrack: volume^2, pitch octaves x bendSense / 3, combined with the parent's"""
        v = self.params[0] ** 2
        o = self.params[1] * self.bend / 3.0
        if self.parent:
            self.mix = dict(vol=self.parent.mix["vol"] * v, pitch=self.parent.mix["pitch"], oct=self.parent.mix["oct"] + o)
        else:
            self.mix = dict(vol=v * self.se_vol, pitch=1.0, oct=o)

    def step(self, chans):
        op = self.u8()
        if op < 0x80:
            flags, vel = self.u8(), self.u8()
            nid = flags & 7
            key = op
            if nid == 0:
                self.gate(key, vel, self.vlq(), flags, chans)
            else:
                self.note_on(nid, key, vel, chans)
            return
        if op & 0xF0 == 0x80:
            if op & 7:
                self.note_off(op & 7)
            return
        over = 0
        if op & 0xF0 == 0x90:
            n, mask, bits = (op & 7) + 1, self.u8(), 3
            for _ in range(n):
                if mask & 0x80:
                    over |= bits
                mask, bits = (mask << 1) & 0xFF, bits << 2
            op = self.u8()
        if op == 0xB0:
            self.u8()
            return
        na, widths = CMD.get(op, (0, 0))
        rt, a = widths | over, []
        if op == 0xF0:
            self.wait = self.vlq()
            return
        if op == 0xFD:
            s = self.seq[self.pc:].split(b"\0")[0]
            self.pc += len(s) + 1 + s.count(b"%")
            return
        for _ in range(na):
            w = rt & 3
            a.append(self.u8() if w == 0 else self.u16() if w == 1 else self.u24() if w == 2 else self.reg(self.u8()))
            rt >>= 2
        self.exec(op, a, chans)

    def exec(self, op, a, chans):
        if op == 0xB1:
            self.note_on(a[1], a[0], a[2], chans)
        elif op == 0xB2:
            self.note_off(a[0])
        elif op == 0xB3:
            self.gate(a[1], a[2], a[3], a[0], chans)
        elif op == 0xB4:
            self.last_key = a[0] + self.transpose_total()
        elif op in (0xB8, 0xB9, 0xBA, 0xBB):
            v = a[1]
            if op in (0xB8, 0xBA):
                s = (v << 8) & 0xFFFF
                if not v & 0x80:
                    s |= (v << 1) & 0xFF
                s = s - 0x10000 if s & 0x8000 else s
            else:
                s = v - 0x10000 if v & 0x8000 else v
            val, n = s / 32767.0, (a[2] if op in (0xBA, 0xBB) else 0)
            if a[0] < 6:
                if n:
                    self.ramps[a[0]] = [val, n]
                else:
                    self.params[a[0]], self.ramps[a[0]] = val, [val, 0]
        elif op == 0xC1:
            ch = Track(self.seq, self.baa, a[1], self)
            self.children[a[0]] = ch
        elif op == 0xC2:
            c = self.children.get(a[0])
            if c:
                c.done = True
        elif op in (0xC3, 0xC4):
            if op == 0xC3 or self.cond(a[0]):
                self.stack.append(self.pc)
                self.pc = a[-1]
        elif op in (0xC5, 0xC6):
            if op == 0xC5 or self.cond(a[0]):
                self.pc = self.stack.pop()
        elif op in (0xC7, 0xC8):
            if op == 0xC7 or self.cond(a[0]):
                self.pc = a[-1]
        elif op in (0xC9, 0xCA):
            t = U32(self.seq, a[1] + 3 * a[0] - 1) & 0xFFFFFF
            if op == 0xCA:
                self.stack.append(self.pc)
            self.pc = t
        elif op == 0xCB:
            self.stack.append(("loop", self.pc, a[0]))
        elif op == 0xCC:
            _, at, n = self.stack[-1]
            n = n - 1 if n else 0
            if not n:
                self.stack.pop()
            else:
                self.stack[-1] = ("loop", at, n)
                self.pc = at
        elif op in (0xD0, 0xD6, 0xD7):
            self.wreg(a[1], 0)
        elif op in (0xD2, 0xD3):
            self.regs[3] = 0
        elif op == 0xD8:
            self.wreg(a[0], a[1])
        elif op in (0xD9, 0xDA):
            o, r, v = a
            cur = self.reg(r)
            res = {0: v, 1: v + cur, 2: cur - v, 4: v * cur, 5: v & cur, 6: v | cur, 7: v ^ cur,
                   8: (np.random.randint(0, 1 << 16) % v) if v else 0, 9: cur << v, 10: cur >> v}.get(o)
            if o == 3:
                self.regs[3] = (cur - v) & 0xFFFF
            elif o == 4:
                self.wreg(33, v * cur)
            elif res is not None:
                self.wreg(r, res)
        elif op == 0xDB:
            self.wreg(a[1], -self.reg(a[1]))
        elif op == 0xDC:
            o, r, t, i = a
            w = {12: 1, 13: 2, 14: 3, 15: 4}.get(o, 4)
            at = t + i * w if o != 16 else t + i
            self.wreg(r, int.from_bytes(self.seq[at:at + w], "big") if o != 16 else U32(self.seq, at))
        elif op == 0xE0:
            self.tempo = a[0]
        elif op == 0xE1:
            self.bank, self.prog = a[0] >> 8, a[0] & 0xFF
        elif op == 0xE2:
            self.bank = a[0]
        elif op == 0xE3:
            self.prog = a[0]
        elif op == 0xE7:
            if self.osc_over[a[0]]:
                self.osc_over[a[0]] = dict(self.osc_over[a[0]], scale=a[1] / 16383)
        elif op == 0xE8:
            t = points(self.seq, a[1])
            self.osc_over[a[0]] = dict(target=a[0], rate=1.0, table=t, rel=[], scale=1.0, vertex=0.0)
        elif op == 0xE9:
            A, D, S, L, R = [x - 0x10000 if x & 0x8000 else x for x in a]
            self.osc_over[0] = dict(target=0, rate=1.0, table=[(0, A, 0x7FFF), (0, D, 0x7FFF), (0, S, L), (0xE, 0, 0)],
                                    rel=[], scale=1.0, vertex=0.0)
            self.dr = a[4]
        elif op == 0xF1:
            self.wait = a[0]
        elif op == 0xF9:
            self.wreg(3, 0xFFFF)
        elif op == 0xFF:
            self.done = True

    def note_on(self, nid, key, vel, chans):
        key += self.transpose_total()
        self.note_off(nid)
        c = self.start(key, vel, 0)
        if c:
            self.notes[nid] = c
            chans.append(c)

    def gate(self, key, vel, time, flags, chans):
        """JASSeqParser::execNoteOnGate + JASTrack::gateOn"""
        f = (2 if flags & 0x40 else 0) | (1 if flags & 0x80 else 0) | (4 if time == 0 else 0)
        key += self.transpose_total()
        t = time * (self.gate_rate / 100.0)
        dsp = int(t / self.interval()) if t else 0
        timer = 0 if f & 6 else dsp
        pitch = self.last_key if f & 1 else key
        if not self.ready:
            self.note_off(0)
            c = self.start(pitch, vel, timer)
            if c:
                self.notes[0] = c
                chans.append(c)
        else:  # tie: the playing channel retuned
            c = self.notes.get(0)
            if c:
                if c.base is not None:
                    c.key = pitch - c.base
                c.vel, c.timer = vel, timer
        c = self.notes.get(0)
        if f & 1 and c:
            delta = key - pitch
            if dsp == 0:
                c.sweep = delta
            else:
                c.sweep, c.sweep_to = 0.0, delta
            c.sweep_n = dsp
        self.last_key = key
        self.ready = bool(f & 2)
        if time:
            self.wait = time
        else:
            self.wait_note = True


def effect(effs, key, vel, vol, pitch):
    """JASInstRand (base value) / JASInstSense on volume and pitch"""
    for e in effs:
        if e[0] == "rand":
            r = e[2]
        else:
            _, typ, st, mp, a, b = e
            x = vel if st == 1 else key
            if mp in (0, 127):
                r = a + x * (b - a) / 127
            elif x < mp:
                r = a + (1 - a) * x / mp
            else:
                r = 1 + (b - 1) * (x - mp) / (127 - mp)
        typ = e[1]
        if typ == 0:
            vol *= r
        elif typ == 1:
            pitch *= r
    return vol, pitch


def render(baa, name, seconds=6.0, ports=None):
    """-> (float32 mono PCM at the DAC rate, ended) for SE `name`"""
    sid = baa.ids[name]
    _, vol = baa.se_item(sid)
    root = Track(baa.d[baa.bsc:], baa, baa.se_seq(sid), se_vol=vol)
    for k, v in (ports or {}).items():
        root.ports[k] = v
    chans, out, acc = [], [], 1.0
    for _ in range(int(seconds * DAC / SUB)):
        while acc >= 1.0:
            acc -= 1.0
            root.tick(chans)
        acc += root.interval()
        buf = np.zeros(SUB, np.float32)
        for c in chans:
            if c.alive:
                c.render(buf)
        chans = [c for c in chans if c.alive]
        out.append(buf)
        if root.done and not chans and all(ch.done for ch in all_tracks(root)):
            return np.concatenate(out), True
    return np.concatenate(out), False


def all_tracks(t):
    out = list(t.children.values())
    for c in t.children.values():
        out += all_tracks(c)
    return out


def write_wav(path, pcm, rate=32000):
    s = (np.clip(pcm, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(s.tobytes())


SPU_FILTERS = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))  # psx-spx SPU ADPCM filters (/ 64)


def spu_adpcm(pcm, loop=None):
    """float PCM -> SPU ADPCM blocks (16 bytes: shift | filter << 4, flags, 28 nibbles low first) (refs/psx-spx-spu.md
    "SPU ADPCM Samples"): per block the filter with the smallest peak prediction residual, the smallest shift that
    holds it, encoded with the decoder's own history. loop (start, end) in samples, multiples of 28: the start block
    flagged 4, the last 3 (end + repeat); one-shot: the last block 1 (end + mute), after one silent block so the
    end flag does not cut the sound's tail. -> bytes"""
    s = np.round(np.clip(pcm, -1, 1) * 32767).astype(np.int64)
    if loop:
        s = s[:loop[1]]
    else:
        s = np.concatenate([s, np.zeros(28, np.int64)])
    s = np.concatenate([s, np.zeros(-len(s) % 28, np.int64)])
    out = bytearray()
    h1 = h2 = 0
    nb = len(s) // 28
    for b in range(nb):
        x = s[28 * b:28 * b + 28]
        best = None
        for fi, (f0, f1) in enumerate(SPU_FILTERS):
            xp = np.concatenate([[h2, h1], x])  # residual against the original signal (an estimate)
            res = x - (xp[1:29] * f0 + xp[0:28] * f1) / 64
            peak = np.abs(res).max()
            if best is None or peak < best[0]:
                best = (peak, fi)
        fi = best[1]
        peak = best[0]
        shift = 12
        while shift > 0 and peak > 7 << (12 - shift):
            shift -= 1
        f0, f1 = SPU_FILTERS[fi]
        nib = []
        for v in x:
            pred = (h1 * f0 + h2 * f1 + 32) >> 6
            q = int(round((v - pred) * (1 << shift) / 4096.0))
            q = -8 if q < -8 else 7 if q > 7 else q
            d = ((q << 12) >> shift) + pred
            d = -32768 if d < -32768 else 32767 if d > 32767 else d
            h2, h1 = h1, d
            nib.append(q & 0xF)
        flags = 0
        if loop and b == loop[0] // 28:
            flags |= 4
        if b == nb - 1:
            flags |= 3 if loop else 1
        out += bytes([shift | fi << 4, flags]) + bytes(nib[2 * i] | nib[2 * i + 1] << 4 for i in range(14))
    return bytes(out)


def spu_decode(b):
    """SPU ADPCM -> int samples (for the selftest)"""
    out, h1, h2 = [], 0, 0
    for o in range(0, len(b), 16):
        shift, fi = b[o] & 0xF, b[o] >> 4
        f0, f1 = SPU_FILTERS[fi]
        for i in range(28):
            q = (b[o + 2 + i // 2] >> (4 * (i & 1))) & 0xF
            q = q - 16 if q >= 8 else q
            d = ((q << 12) >> shift) + ((h1 * f0 + h2 * f1 + 32) >> 6)
            d = -32768 if d < -32768 else 32767 if d > 32767 else d
            h2, h1 = h1, d
            out.append(d)
    return out


def ps2_wave(pcm, rate, loop, cap):
    """a wave for SPU2: resampled to at most `cap` Hz (scipy resample_poly), a loop's length made a multiple of 28
    samples (the rate kept exact: the stored rate is the effective one) and its start moved onto a block by leading
    silence -> (adpcm bytes, rate, samples, loop start or None)"""
    from fractions import Fraction
    from scipy.signal import resample_poly
    r = min(1.0, cap / rate)
    if loop and loop[1] > loop[0]:
        ln = loop[1] - loop[0]
        r = max(28, round(ln * r / 28) * 28) / ln
    fr = Fraction(r).limit_denominator(512)
    out = resample_poly(pcm, fr.numerator, fr.denominator).astype(np.float32) if fr != 1 else pcm
    r = fr.numerator / fr.denominator
    lp = None
    if loop and loop[1] > loop[0]:
        ls, le = round(loop[0] * r), round(loop[0] * r) + round((loop[1] - loop[0]) * r / 28) * 28
        pad = -ls % 28
        out = np.concatenate([np.zeros(pad, np.float32), out])
        if len(out) < le + pad:
            out = np.concatenate([out, np.zeros(le + pad - len(out), np.float32)])
        lp = (ls + pad, le + pad)
    return spu_adpcm(out, lp), rate * r, (lp[1] if lp else len(out)), (lp[0] if lp else None)


# archives whose animation sound tables (.bas) go into sound.bin: Mario's and Good Egg's actors' (resident ones too)
BAS_ARCS = ("MarioAnime", "Butler", "CocoSamboBody", "CocoSamboHead", "DinoPackun", "GrandStar", "Karipon", "Kinopio",
            "Kuribo", "KuriboMini", "OtaKing", "OtaKingLv2", "PackunFlower", "PackunPetit", "SpinTico", "Takobo", "Tico",
            "TicoBaby", "TicoComet", "TicoFat")


def mario_table(out_h):
    """Petari Player/MarioSound.cpp `soundlist` (key, SE, flags, the companion key played under water) ->
    engine/src/mario_snd.h for Mario::playSoundJ: kind = flags byte 0 (& 3: 0 on Mario, 1 level on Mario, 2 system,
    3 system level; & 0xC: the companion too under water)"""
    src = (Path(__file__).resolve().parent.parent / "vendor/Petari/src/Game/Player/MarioSound.cpp").read_text("utf-8")
    body = src[src.index("SoundList soundlist[] = {"):]
    body = body[:body.index("\n};")]
    ents = re.findall(r'\{\s*"([^"]*)",\s*//[^\n]*\n\s*([A-Za-z0-9_]+),[^\n]*\n\s*([^,]+),[^\n]*\n\s*([^,]+),', body)
    rows = []
    for key, se, flags, comp in ents:
        if not key:
            continue
        comp = comp.strip()
        rows.append(f'    {{"{key}", {se}, {int(flags, 0) >> 24}, {comp if comp != "0" else "NULL"}}},\n')
    Path(out_h).write_text("/* Mario's sound table, Petari Player/MarioSound.cpp soundlist (tools/sound.py pack) */\n"
                           "static const struct { const char *key; u32 id; u8 kind; const char *water; } mario_snd[] = {\n"
                           + "".join(rows) + "};\n", "utf-8")
    return len(rows)


def scene_types(scene_bin):
    """the placed class names of a scene.bin (its TYPE chunk: u32 n, n x name[32])"""
    b = Path(scene_bin).read_bytes()
    o = 16
    while o < len(b):
        tag, size = b[o:o + 4], struct.unpack_from("<I", b, o + 4)[0]
        if tag == b"TYPE":  # chunks: tag, u32 size, 8 pad, the payload (16-aligned): tools/slice.py chunk()
            n = struct.unpack_from("<I", b, o + 16)[0]
            return {b[o + 20 + 32 * i:o + 52 + 32 * i].split(b"\0")[0].decode() for i in range(n)}
        o += 16 + size
    return set()


def scene_talk_sounds(scene_bin):
    """the talk sound numbers (Msg info byte 6) of a scene.bin's TALK chunk (tools/msg.py)"""
    b = Path(scene_bin).read_bytes()
    o = 16
    while o < len(b):
        tag, size = b[o:o + 4], struct.unpack_from("<I", b, o + 4)[0]
        if tag == b"TALK":
            nctl, nnode, nmsg = struct.unpack_from("<HHH", b, o + 16)
            m = o + 32 + 40 * nctl + 12 * nnode
            return {b[m + 12 * i + 6] for i in range(nmsg)}
        o += 16 + size
    return set()


def scenario_keep(placed):
    """the SE names a scenario may play, by phase 0c's rule (tools/sfxcut.py: Petari's code per class, placement):
    -> (names some placed class or global code names, every name some class file names). A name no placed class
    and no global code names, but some class does, is left out; a name no file names (sound tables) stays."""
    import sfxcut as C
    cut = C.Cutter()
    files = cut.files_for(placed)
    # a class's helper files (its states, demos, parts: DinoPackunStateDamage.cpp) share its name's start; sfxcut only
    # follows `new`, which misses members built other ways
    classes = {cut.classes[n.lower()] for n in placed if n.lower() in cut.classes} | set(placed)
    files |= {f for f in cut.sounds if any(f.rsplit("/", 1)[-1].startswith(c) for c in classes)}
    keep = set(cut.global_names)
    for f in files:
        keep |= cut.sounds[f]
    for n in placed:
        keep |= cut.stage_fx.get(n, set())
    known = set().union(*cut.sounds.values())
    return keep, known


# AudMeSeqParser::sCmdInfo: opcode -> argument widths (0 u8, 1 u16, 2 u24)
ME_ARGS = {0x00: (0,), 0x01: (0,), 0x02: (1,), 0x03: (1,), 0x04: (1,) * 5, 0x10: (0, 2), 0x11: (0,), 0x20: (0,),
           0x28: (0, 1), 0x2A: (0,), 0x30: (2,), 0x31: (2,), 0x33: (1,), 0x35: (2,), 0x36: (2,), 0x3A: (2,), 0x3B: (2,),
           0x40: (2, 0), 0x41: (2, 0, 0), 0x42: (2, 0, 0), 0x43: (2, 0, 0, 0), 0x50: (0,), 0x51: (0,), 0x52: (0,),
           0x53: (0,), 0x54: (0,), 0x58: (0,), 0x59: (0,), 0x5A: (1,), 0x5B: (1,), 0x5C: (0,), 0x5D: (1,), 0x60: (0,),
           0x68: (0, 0), 0x69: (0, 0), 0x78: (2,), 0x79: (2,), 0x81: (2,), 0x82: (2,), 0x83: (2,), 0x84: (0,),
           0x86: (0,), 0x87: (0,), 0x8A: (0, 0), 0x90: (0, 0, 0), 0x91: (0, 0, 0)}


# the placed classes (patterns from the start of the name) that start an ME (Petari's MR::startSystemME callers); the
# rest play anywhere
ME_CLASSES = {"ME_MAGIC": ("SpinDriver", "Karikari"), "ME_MAGIC_L": ("SuperSpinDriver",), "ME_RALLY_COMBO": ("OtaKing$",),
              "ME_FLIP_PANEL": ("FlipPanelObserver",), "ME_ASTRO_DOME": ("Mini.*Galaxy$", "FileSelector")}


def me_res(fs):
    """JaiMe.arc (AudSystem::setMeResource(arc, 1, 0)) -> (metable.bmt, defaultme.bme, {ME name: id})"""
    from budget import rarc_files
    arc = dict(rarc_files((Path(fs) / "AudioRes/Info/JaiMe.arc").read_bytes()))
    tab, seq = arc["metable.bmt"], arc["defaultme.bme"]
    n, names = U32(tab, 0), U32(tab, 8)
    return tab, seq, {tab[U32(tab, names + 4 * i):].split(bytes(1))[0].decode(): i for i in range(n)}


def me_programs(seq, me_id):
    """(bank, program) pairs an ME's notes play: its tracks read straight through (children opened with their
    parent's bank / program, AudMeTrack::inherit), as AudMeSeqParser's cmdBank / cmdPrg / cmdBProg set them"""
    out, todo, seen = set(), [(U32(seq, 4 + 4 * me_id), 0, 0xF0)], set()
    while todo:
        pc, bank, prog = todo.pop()
        while pc < len(seq) and pc not in seen:
            seen.add(pc)
            op, a, p = seq[pc], [], pc + 1
            if op not in ME_ARGS and op != 0x24 and op not in (0x2B, 0x32, 0x34, 0x38, 0x39, 0x48, 0x49, 0x4A, 0x55,
                                                                0x70, 0x71, 0x80, 0x85):
                break
            for w in ME_ARGS.get(op, ()):
                a.append(int.from_bytes(seq[p:p + w + 1], "big"))
                p += w + 1
            if op == 0x24:
                break
            if op == 0x00:
                bank = a[0]
            elif op == 0x01:
                prog = a[0]
            elif op == 0x02:
                bank, prog = a[0] >> 8, a[0] & 0xFF
            elif op == 0x10:
                todo.append((a[1], bank, prog))
            elif op == 0x40:
                out.add((bank, prog))
            elif op == 0x30:
                p = a[0]
            pc = p
    return out


def pack(fs, out_bin, ids_h, sources, cap=16000, budget=1850000, scene=None):
    """sound.bin for the engine (se.c) and se_ids.h (every SE name -> its JAISoundID):
    'SNDB', then u32 pairs (offset, count / size): BSC bytes, banks {u16 bank, u16 wsys, u32 IBNK offset} (8 B),
    SEs {u32 id, u32 sequence offset in the BSC, u8 priority, u8 volume (x 255), u16 pad} (12 B, sorted by id),
    waves {u16 wsys, u16 wave id, u32 SPU offset, u32 samples, u32 loop start (0xFFFFFFFF none), f32 rate,
    u8 base key, 3 pad} (24 B, sorted by wsys << 16 | id), the ADPCM blob (uploaded to SPU RAM as one), then
    (offset, count) of the animation sound index {char "<archive>/<clip>"[32], u32 offset, u32 entries} (40 B,
    sorted), the offset of its entries (JAUSoundAnimationSound, 0x20 B each, big-endian as in the .bas), then
    the ADPCM offsets of Mario's voice waves and of Luigi's (the last; their rows point where they go: over Mario's).
    IBNK / BSC stay big-endian. The SEs are the names written in `sources`; their waves are every one their
    sequences reach (bms analysis, all variants of keys read from registers), resampled to at most `cap` Hz for
    SYSTEM / PLAYER sounds and cap x 11025 / 16000 for the rest, all scaled down until the ADPCM fits `budget` bytes
    of SPU RAM (2 MB less audsrv's area under 0x10000 and some room). Then (offset, offset) of JaiMe.arc's metable.bmt
    and defaultme.bme (engine/src/me.c; 0: none), the waves of the MEs the sources name: every key of their programs
    (the notes follow the BGM's chord)."""
    import audio as A
    baa = Baa(fs)
    d = baa.d
    mario_h = Path(ids_h).with_name("mario_snd.h")
    mario_table(mario_h)
    names = {n for src in sources for n in re.findall(r"\bSE_[A-Z0-9_]+\b", Path(src).read_text(errors="ignore"))
             if n in baa.ids and baa.ids[n][0] == 0}
    # Mario's table: only the entries the sources play (mario_sound("<key>"), its 声 voice, its water companion)
    keys = {k for src in sources for line in Path(src).read_text("utf-8", "ignore").splitlines()
            if "mario_sound(" in line for k in re.findall(r'"([^"]+)"', line)}  # ternaries too
    for key, se, kind, comp in re.findall(r'\{"([^"]+)", (SE_[A-Z0-9_]+), (\d+), ("[^"]*"|NULL)\}',
                                          mario_h.read_text("utf-8")):
        if key in keys or (key.startswith("声") and key[1:] in keys):
            names.add(se)
    # MR::startTalkSound's table (game.c talk_snd, cTalkSoundList order): only the numbers a scenario's messages use
    talk = []
    for src in sources:
        m = re.search(r"talk_snd\[\] = \{(.*?)\};", Path(src).read_text("utf-8", "ignore"), re.S)
        if m:
            talk = [t.strip() for t in m.group(1).split(",") if t.strip()]
    names -= set(talk)
    if scene:
        names |= {talk[n] for n in scene_talk_sounds(scene) if n < len(talk) and talk[n] in baa.ids}
    bas = {}  # "<archive>/<clip>" lower case -> the .bas bytes' entries (0x20 each, big-endian as on disc)
    from budget import rarc_files
    for arc in BAS_ARCS:
        for n, b in rarc_files((Path(fs) / "ObjectData" / f"{arc}.arc").read_bytes()):
            if n.lower().endswith(".bas"):
                cnt = U16(b, 0)
                bas[f"{arc.lower()}/{n.rsplit('/', 1)[-1][:-4].lower()}"] = b[8:8 + 0x20 * cnt]
                for k in range(cnt):
                    w = U32(b, 8 + 0x20 * k)
                    nm = baa.names.get((w >> 24, (w >> 16) & 0xFF, w & 0xFFFF))
                    if nm and w >> 24 == 0:
                        names.add(nm)
    if scene:  # a scenario's own pack: SYSTEM / PLAYER sounds always, the rest when a placed class may play them
        placed = scene_types(scene)
        keep, known = scenario_keep(placed)
        placed_l = {t.lower() for t in placed}
        bas = {k: v for k, v in bas.items() if k.split("/")[0] in placed_l or k.startswith("marioanime/")}
        bas_names = {baa.names.get((U32(v, 0x20 * i) >> 24, (U32(v, 0x20 * i) >> 16) & 0xFF, U32(v, 0x20 * i) & 0xFFFF))
                     for v in bas.values() for i in range(len(v) // 0x20)}
        names = {n for n in names if baa.ids[n][1] in (0, 1, 2) or n in keep or n not in known or n in bas_names
                 or n in talk}
    names = sorted(names)
    Path(ids_h).write_text("/* SE names -> JAISoundID (section << 24 | group << 16 | index), tools/sound.py pack */\n"
                           "#ifndef SE_IDS_H\n#define SE_IDS_H\n" +
                           "".join(f"#define {n} 0x{s << 24 | g << 16 | i:08X}u\n" for (s, g, i), n in
                                   sorted(baa.names.items()) if s == 0 and re.fullmatch(r"SE_[A-Z0-9_]+", n)) +
                           "#endif\n")
    inst = A.instruments(d)
    A.DYN_KEY = "all"
    bsc = d[baa.bsc:U32(d, baa.bsc + 4) + baa.bsc]
    waves = {}  # (wsys, wave id) -> its rate cap: the highest of its sounds' categories (SYSTEM / PLAYER_* high)
    se_rows = []
    for n in names:
        sid = baa.ids[n]
        off = baa.se_seq(sid)
        c = cap if sid[1] in (0, 1, 2) else cap * 11025 // 16000
        for w in A.reached([(bsc, off)], inst)[0]:
            waves[w] = max(waves.get(w, 0), c)
        pr, vol = baa.se_item(sid)
        se_rows.append((sid[1] << 16 | sid[2], off, pr, round(vol * 255)))
    # Luigi's voice (AudSystem::switchToLuigi: an SE_PV sound gets port 12 = 20, which moves the voice bank's programs
    # 0-2 to 20-22: wsys 34's waves 0-79 instead of 80-159; AudSceneMgr::loadPlayerResource loads one set or the
    # other). Both sets packed, Luigi's last; the engine uploads one of them at the same SPU offset (se_load).
    vprogs, to_luigi = inst[VOICE_BANK][1], {}
    for p in (0, 1, 2):  # the same key regions, Luigi's waves
        for (h, m), (hl, lg) in zip(vprogs[p], vprogs[p + 20], strict=True):
            assert h == hl and len(m) == len(lg) == 1, (p, h)
            to_luigi[(VOICE_WSYS, *m)] = (VOICE_WSYS, *lg)
    voice = sorted(w for w in waves if w in to_luigi)
    luigi = sorted({to_luigi[w] for w in voice})
    for v in voice:
        waves[to_luigi[v]] = max(waves.get(to_luigi[v], 0), waves[v])
    if scene:  # AudSceneMgr: only the wave archives the stage / scenario loads (static, system, player, its BGM / SE /
        # scenario SE sets); a sound whose waves are elsewhere is silent on the Wii too. Unknown scenes keep all.
        sets = A.scene_sets()
        gal, no = Path(scene).parts[-3], Path(scene).parts[-2]
        loaded = sets.get((gal, int(no) if no.isdigit() else 0), sets.get((gal, 0)))
        if loaded:
            arc = baa.wave_archives()
            drop = {w for w in waves if w[0] != VOICE_WSYS and
                    not any(b == w[0] and (i is None or i == arc.get(w)) for b, i in loaded)}
            print(f"{len(drop)} waves in archives {gal} {no} does not load")
            for w in drop:
                del waves[w]
    me_tab, me_seq, me_ids = me_res(fs)
    me_tok = {t for src in sources for t in re.findall(r"\bME_[A-Z0-9_]+\b", Path(src).read_text(errors="ignore"))}
    me_names = sorted(n for n in me_ids if n in me_tok or any(n[:len(t)] == t and n[len(t):].isdigit() for t in me_tok))
    # (a stem me_start_random numbers: ME_ASTRO_DOME_SELECT -> 1..4); a scenario's own pack: those its classes play
    if scene:
        placed = scene_types(scene)
        def me_keep(n):
            for pre, cls in sorted(ME_CLASSES.items(), key=lambda kv: -len(kv[0])):
                if n.startswith(pre):
                    return any(re.match(c, t) for t in placed for c in cls)
            return True
        me_names = [n for n in me_names if me_keep(n)]
    for bank, prog in sorted({bp for n in me_names for bp in me_programs(me_seq, me_ids[n])}):
        if bank in inst:
            wb, progs = inst[bank]
            for _, ids in progs.get(prog, []):
                for w in ids:
                    waves[(wb, w)] = max(waves.get((wb, w), 0), cap)
    rest = sorted(set(waves) - set(voice) - set(luigi))
    info = {w: baa.wave(*w) for w in waves}

    def est(k):  # SPU ADPCM bytes with every cap scaled by k: the rest and one voice set
        def size(ws):
            return sum(len(info[w][0]) * min(1.0, waves[w] * k / info[w][1]) / 28 * 16 for w in ws)
        return size(rest) + max(size(voice), size(luigi))
    scale = 1.0
    while est(scale) > budget and scale > 0.3:
        scale *= 0.95
    blob, wrows, starts = bytearray(), [], []
    for ws, wid in rest + voice + luigi:
        if (ws, wid) in (voice[:1] + luigi[:1]):  # each voice set 64-aligned (SPU uploads)
            blob += bytes(-len(blob) % 64)
            starts.append(len(blob))
        pcm, rate, base, loop = info[(ws, wid)]
        c = round(waves[(ws, wid)] * scale)
        cf = Path("out/sound_cache") / f"{ws}_{wid}_{c}.bin"
        try:  # (another job may be replacing it)
            d_ = cf.read_bytes() if cf.exists() else None
        except OSError:
            d_ = None
        if d_:
            r2, n, ls = struct.unpack_from("<fIi", d_, 0)
            adp, ls = d_[12:], (None if ls < 0 else ls)
        else:
            adp, r2, n, ls = ps2_wave(pcm, rate, loop, c)
            cf.parent.mkdir(parents=True, exist_ok=True)
            tmp = cf.with_suffix(f".{os.getpid()}")  # whole files only: packs are built in parallel
            tmp.write_bytes(struct.pack("<fIi", r2, n, -1 if ls is None else ls) + adp)
            try:
                os.replace(tmp, cf)
            except OSError:  # another job's copy in use
                tmp.unlink()
        at = len(blob) - (starts[1] - starts[0] if len(starts) == 2 else 0)  # Luigi's: where they are uploaded
        wrows.append(struct.pack("<HHIIIfB3x", ws, wid, at, n, 0xFFFFFFFF if ls is None else ls, r2, base))
        blob += adp
    o_voice, o_luigi = starts or (0, 0)
    wrows.sort(key=lambda r: struct.unpack_from("<HH", r))
    banks = sorted(baa.banks.items())
    ib_blob, brows = bytearray(), []
    for bno, (off, ws) in banks:
        size = U32(d, off + 4)
        brows.append((bno, ws, len(ib_blob)))
        ib_blob += d[off:off + size] + bytes(-size % 16)
    head = 80
    o_bsc = head
    o_bank = o_bsc + len(bsc) + (-len(bsc) % 16)
    o_ibnk = o_bank + 8 * len(brows) + (-(8 * len(brows)) % 16)
    o_se = o_ibnk + len(ib_blob)
    o_wv = o_se + 12 * len(se_rows) + (-(12 * len(se_rows)) % 16)
    o_bas = o_wv + 24 * len(wrows) + (-(24 * len(wrows)) % 16)
    bkeys = sorted(bas)
    o_basent = o_bas + 40 * len(bkeys)
    basent = b"".join(bas[k] for k in bkeys)
    o_metab = o_basent + len(basent) + (-(o_basent + len(basent)) % 16)
    o_meseq = o_metab + len(me_tab) + (-len(me_tab) % 16)
    o_adp = o_meseq + len(me_seq) + (-(o_meseq + len(me_seq)) % 64)
    out = bytearray(o_adp + len(blob))
    struct.pack_into("<4s17I", out, 0, b"SNDB", o_bsc, len(bsc), o_bank, len(brows), o_se, len(se_rows), o_wv,
                     len(wrows), o_adp, len(blob), o_bas, len(bkeys), o_basent, o_voice, o_luigi, o_metab, o_meseq)
    out[o_metab:o_metab + len(me_tab)] = me_tab
    out[o_meseq:o_meseq + len(me_seq)] = me_seq
    at = 0
    for k, key in enumerate(bkeys):
        struct.pack_into("<32sII", out, o_bas + 40 * k, key.encode(), at, len(bas[key]) // 0x20)
        at += len(bas[key])
    out[o_basent:o_basent + len(basent)] = basent
    out[o_bsc:o_bsc + len(bsc)] = bsc
    for k, (bno, ws, io) in enumerate(brows):
        struct.pack_into("<HHI", out, o_bank + 8 * k, bno, ws, o_ibnk + io)
    out[o_ibnk:o_ibnk + len(ib_blob)] = ib_blob
    se_rows.sort()
    for k, r in enumerate(se_rows):
        struct.pack_into("<IIBBxx", out, o_se + 12 * k, *r)
    for k, r in enumerate(wrows):
        out[o_wv + 24 * k:o_wv + 24 * k + 24] = r
    out[o_adp:] = blob
    Path(out_bin).write_bytes(out)
    print(f"sound.bin: {len(names)} SEs, {len(me_names)} MEs, {len(wrows)} waves, ADPCM {len(blob) / 1e6:.2f} MB (caps x {scale:.2f}: system "
          f"and Mario {cap * scale:.0f} Hz, the rest {cap * 11025 // 16000 * scale:.0f} Hz), "
          f"EE {o_adp / 1e3:.0f} KB, SPU {max(o_luigi, len(blob) - o_luigi + o_voice) / 1e6:.2f} MB (voices: Mario "
          f"{(o_luigi - o_voice) / 1e3:.0f} KB, Luigi {(len(blob) - o_luigi) / 1e3:.0f} KB)")


VOICE_BANK, VOICE_WSYS = 70, 34  # the player voice's IBNK and wave system (B70kawa)
CURVES = _curves()


def selftest():
    # AFC: a zero frame with coefficient 0 decodes silent; one nibble of 1 at scale 1 gives 1 << 11 >> 11 = 1
    assert decode(bytes([0x00] + [0] * 8), 0, 16).tolist() == [0.0] * 16
    assert decode(bytes([0x00, 0x10] + [0] * 7), 0, 16)[0] * 32768 == 1
    # 8-bit parameter expansion: 0x7F -> 0.99994, 0x80 -> -1
    o = Osc(dict(target=0, rate=1.0, table=[(0, 600, 32767), (0xE, 0, 0)], rel=[(0, 600, 0), (0xF, 0, 0)],
                 scale=1.0, vertex=0.0))
    for _ in range(int(DAC / SUB) + 2):  # 1 s and a bit
        o.inc(48000.0 / DAC)
    assert o.state == 2 and abs(o.value() - 1.0) < 1e-3, (o.state, o.value())
    # SPU ADPCM round trip: a 440 Hz tone at 8 kHz comes back within 2 % of full scale; flags end / loop
    t = 0.5 * np.sin(np.arange(2800) * 2 * np.pi * 440 / 8000)
    b = spu_adpcm(t)
    back = np.array(spu_decode(b)[:2800]) / 32767
    assert np.abs(back - t).max() < 0.02, np.abs(back - t).max()
    assert b[-15] == 1 and len(b) % 16 == 0
    b = spu_adpcm(t, (280, 2800))
    assert b[16 * 10 + 1] == 4 and b[-15] == 3
    print("sound selftest ok")


if __name__ == "__main__":
    if sys.argv[1] == "--selftest":
        selftest()
        sys.exit(0)
    if sys.argv[1] == "--pack":  # --pack fs out.bin engine/src/se_ids.h [cap Hz] [--scene scene.bin] sources...
        a = sys.argv[2:]
        scene = None
        if "--scene" in a:
            scene = a[a.index("--scene") + 1]
            a = [x for x in a if x not in ("--scene", scene)]
        cap = int(a[3]) if a[3].isdigit() else 16000
        pack(a[0], a[1], a[2], [x for x in a[3:] if not x.isdigit()], cap, scene=scene)
        sys.exit(0)
    args = sys.argv[2:]
    out = Path("out/sound")
    if "--out" in args:
        out = Path(args[args.index("--out") + 1])
        args = [a for a in args if a != "--out" and a != str(out)]
    out.mkdir(parents=True, exist_ok=True)
    baa = Baa(sys.argv[1])
    for n in args:
        pcm, ended = render(baa, n)
        write_wav(out / f"{n}.wav", pcm)
        print(f"{n}: {len(pcm) / DAC:.2f} s{'' if ended else ' (still playing: cut)'}, peak {np.abs(pcm).max():.3f}")
