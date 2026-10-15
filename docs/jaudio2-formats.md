# JAudio2 (Super Mario Galaxy) sound-effect formats: reference for an offline SE renderer

All multi-byte values are **big-endian**. "Offset" means a byte offset from the start of the containing file unless stated otherwise.

## Sources (in order of authority)

| Tag | Source | Revision |
|---|---|---|
| **PETARI** | SMG decomp, `D:/smg-ps2/vendor/Petari` (github.com/SMGCommunity/Petari): `src/JSystem/JAudio2/*.cpp`, `libs/JSystem/include/JSystem/JAudio2/*.hpp`, `src/Game/AudioLib/AudSoundInfo.cpp` | commit `601f78cb5dfae6c5851d4363a6e22956201f41bd` |
| **TP** | Twilight Princess decomp, github.com/zeldaret/tp, `libs/JSystem/src/JAudio2/*.cpp`, `libs/JSystem/include/JSystem/JAudio2/*.h`, `src/Z2AudioLib/Z2SoundInfo.cpp` | commit `c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0` |
| **VGM** | vgmstream `src/coding/afc_decoder.c` (github.com/vgmstream/vgmstream) | commit `a8ee090f8e6cf822949ba36286abce025fa880fc` |
| **JSX** | xayrga JaiSeqX (github.com/XAYRGA/JaiSeqX), `JaiSeqXLJA/libJAudio/Loaders/*.cs` | commit `3af51437498c12b47f3ff560f2698814b2d402df` |

The **SMG code (PETARI) is the primary source.** TP is an older JAudio2 build. The two have identical sequence parsers, oscillators, channel math and WSYS parsing. They differ in the instrument bank: TP's `JASBasicInst` has **no velocity regions and no Rand/Sens effects** (TP's Ver1 parser keeps only the first velocity region), while SMG has both. **Use the SMG layout described below.**

> **Important correction to the brief.** "0x80 u8 wait, 0x88 u16 wait, 0x9x perf commands, VLQ wait" describes the *JAudio1* BMS format (Sunshine, Pikmin, Wind Waker). It does **not** apply to JAudio2/SMG. In SMG:
> - `0x80–0x8F` is note-off.
> - `0x90–0x9F` is a register-argument prefix.
> - Waits are `0xF0` (VLQ) and `0xF1` (u8).
> - Parameter ("perf") commands are `0xB8–0xBB`.

---

## 1. BMS sequence bytecode (JASSeqParser / JASSeqCtrl / JASSeqReader)

### 1.1 Top-level dispatch (`JASSeqParser::parse`, PETARI JASSeqParser.cpp:915; TP same)

```cpp
u8 data = readByte();
if ((data & 0x80) == 0)              return parseNoteOn(track, data);           // 0x00-0x7F
switch (data & 0xf0) {
case 0x80: return parseNoteOff(track, data & 0xf);                           // 0x80-0x8F
case 0x90: return parseRegCommand(track, (data & 7) + 1);                    // 0x90-0x9F
default:   return parseCommand(track, data, 0);                              // 0xA0-0xFF
}
```

### 1.2 Note on: `0x00–0x7F` (`parseNoteOn`)

```
byte0 = key (0..127)
byte1 = flags:  bits 0-2 = voice/note id (0..7)
                bit 7 (0x80) -> gate flag 1 ("slide from last key")
                bit 6 (0x40) -> gate flag 2 ("tie": reuse the playing channel)
byte2 = velocity (0..127)
if (flags & 7) == 0:  VLQ gate length (ticks) follows   -> execNoteOnGate
else:                 no length                         -> execNoteOnMidi (held until note-off)
```

```cpp
// PETARI JASSeqParser.cpp:850
u8 ctrlFlags = readByte(); u8 noteId = ctrlFlags & 7; u8 velocity = readByte();
if (noteId == 0) execNoteOnGate(track, pitch, velocity, readMidiValue(), ctrlFlags);
else             execNoteOnMidi(track, noteId, pitch, velocity);

void JASSeqParser::execNoteOnGate(JASTrack* track, u32 pitch, u32 velocity, u32 time, u32 ctrlFlags) {
    u32 flags = 0;
    if (ctrlFlags >> 6 & 1) flags |= 2;
    if (ctrlFlags >> 7 & 1) flags |= 1;
    if (time == 0)          flags |= 4;
    track->gateOn(pitch, velocity, time, flags);
    if (time != 0) track->getSeqCtrl()->wait(time);      // the gate note ALSO waits `time` ticks
    else           track->getSeqCtrl()->waitNoteFinish();// time 0: block until channel 0 has stopped
}
```

**Gate notes (voice 0)**, from `JASTrack::gateOn` (TP JASTrack.cpp; PETARI equivalent):

- The key gets the total transpose added. If `gateRate != 100`, the gate time is scaled by `gateRate/100`.
- `dspTime = seqTimeToDspTime(time)` is the number of audio sub-frames (see 1.7).
- If `(flags & 6) == 0`, the channel's update timer is set to `dspTime`. When it reaches 0, the `CB_TIMER` callback calls `release(0)`, so the note releases after its gate.
- When `time == 0` (flag 4) or tie (flag 2), there is no auto-release.
- **Without tie mode** (`flag4` from the *previous* gate note clear), the old voice-0 channel is released and a new channel is started.
- **With tie mode** (the previous gate note had bit 6), the existing channel is retuned: `setKey(key - baseKey)`, then `setVelocity`, then `setUpdateTimer`.
- **Flag 1 (bit 7)** starts the note at the *previous* key (`field_0x22b`, "latest key") and sweeps linearly to the new key over `dspTime` sub-frames (`setKeySweepTarget`). This is portamento.
- **`waitNoteFinish`:** the track does not parse again until `checkNoteStop(0)` is true. The channel pointer is cleared when the channel dies: envelope osc 0 stopped, or the DSP reached the end of a non-looped sample.

**Held notes (voice 1..7):**

- `JASTrack::noteOn(id, key+transpose, vel)` first releases any channel already on that id, then starts a new one.
- It sounds until a note-off for that id.

### 1.3 Note off: `0x80–0x8F` (`parseNoteOff`)

```cpp
u8 note_id = param_1 & 7;  if (note_id) execNoteOff(track, note_id);  // -> noteOff(id, 0) -> channel->release(0)
```

- `0x81–0x87` release voices 1–7.
- `0x80` and `0x88` map to id 0 and do nothing (TP warns "noteoff for note_id == 0").
- `0x89–0x8F` alias `0x81–0x87`.
- No argument bytes follow.

### 1.4 Register-argument prefix: `0x90–0x9F` (`parseRegCommand`)

```cpp
s32 JASSeqParser::parseRegCommand(JASTrack* t, int n /* = (op&7)+1 */) {
    u8 mask = readByte(); u16 override = 0; u16 bits = 3;
    for (int i = 0; i < n; i++) { if (mask & 0x80) override |= bits; mask <<= 1; bits <<= 2; }
    u8 cmd = readByte();
    return parseCommand(t, cmd, override);   // argWidths |= override
}
```

The layout is `9N mask cmd args...`:

- Bit 7 of `mask` marks argument 0, bit 6 argument 1, and so on.
- A marked argument has its width forced to type 3: a u8 register index is read, and the register's value is used.

### 1.5 Commands `0xA0–0xFF`: decompiled `sCmdInfo` and argument decoding

```cpp
s32 JASSeqParser::parseCommand(JASTrack* track, u8 cmdCode, u16 c) {
    Command* cmd = (cmdCode != 0xB0) ? &sCmdInfo[cmdCode - 0xA0] : &sExtCmdInfo[readByte()];
    u32 readType = cmd->argWidths | c;
    for (int i = 0; i < cmd->numArgs; i++) {
        switch (readType & 3) {
        case 0: arg = readByte();  break;              // u8
        case 1: arg = read16();    break;              // u16
        case 2: arg = read24();    break;              // u24
        case 3: arg = readReg(track, readByte()); break; // u8 register index -> register value
        }
        args[i] = arg; readType = (u16)readType >> 2;   // 2 bits per arg, arg0 in the LOW bits
    }
    if (!cmd->func) return 0;                            // NULL entry = 1-byte no-op
    return (this->*cmd->func)(track, args);
}
```

The quoted table is `JASSeqParser::sCmdInfo[0x60]`. It is byte-identical in PETARI (JASSeqParser.cpp:9) and TP (JASSeqParser.cpp:12, format `handler, numArgs, argWidths`). Entries not listed are `NULL, 0, 0`: a 1-byte no-op.

| Op | Handler | nArgs | widths | Bytes after opcode | Semantics |
|---|---|---|---|---|---|
| B0 | (extended) | – | – | u8 sub | `sExtCmdInfo[sub]`; only `sub=1` → `cmdDump` (0 args) |
| B1 | cmdNoteOn | 3 | 0x00 | u8 key, u8 noteId, u8 vel | `noteOn(id=a1, key=a0, vel=a2)` |
| B2 | cmdNoteOff | 1 | 0x00 | u8 id | `noteOff(id)` |
| B3 | cmdNote | 4 | 0x40 | u8 flags, u8 key, u8 vel, **u16** time | `execNoteOnGate(key=a1, vel=a2, time=a3, flags=a0)` |
| B4 | cmdSetLastNote | 1 | 0 | u8 key | latest key = key + transpose (used by slide flag) |
| B8 | cmdParamE | 2 | 0x00 | u8 param, s8 value | `setParam(id, v8→s16/32767, 0)` (see 1.6) |
| B9 | cmdParamI | 2 | 0x04 | u8 param, s16 value | `setParam(id, v/32767, 0)` |
| BA | cmdParamEI | 3 | 0x10 | u8 param, s8 value, u16 ticks | ramp over `ticks` |
| BB | cmdParamII | 3 | 0x14 | u8 param, s16 value, u16 ticks | ramp over `ticks` |
| C1 | cmdOpenTrack | 2 | 0x08 | u8 trackNo(0..15), u24 offset | open child track at `base+offset`, start it |
| C2 | cmdCloseTrack | 1 | 0 | u8 trackNo | close child |
| C3 | cmdCall | 1 | 0x02 | u24 offset | push return, jump |
| C4 | cmdCallF | 2 | 0x08 | u8 cond, u24 offset | conditional call |
| C5 | cmdRet | 0 | – | – | pop |
| C6 | cmdRetF | 1 | 0 | u8 cond | conditional return |
| C7 | cmdJmp | 1 | 0x02 | u24 offset | jump |
| C8 | cmdJmpF | 2 | 0x08 | u8 cond, u24 offset | conditional jump |
| C9 | cmdJmpTable | 2 | 0x0B | **reg** idx, u24 table | `jump(get24(table + idx*3))` |
| CA | cmdCallTable | 2 | 0x0B | **reg** idx, u24 table | `call(get24(table + idx*3))` |
| CB | cmdLoopS | 1 | 0x01 | u16 count | push loop |
| CC | cmdLoopE | 0 | – | – | loop end |
| D0 | cmdReadPort | 2 | 0 | u8 port, u8 reg | reg = import port |
| D1 | cmdWritePort | 2 | 0x0C | u8 port, **reg** value | export port = value |
| D2 | cmdCheckPortImport | 1 | 0 | u8 port | REG_F(3) = import flag |
| D3 | cmdCheckPortExport | 1 | 0 | u8 port | REG_F = export flag |
| D4 | cmdParentWritePort | 2 | 0x0C | u8 port, **reg** value | |
| D5 | cmdChildWritePort | 2 | 0x0C | u8 (child<<4 \| port), **reg** value | |
| D6 | cmdParentReadPort | 2 | 0 | u8 port, u8 reg | |
| D7 | cmdChildReadPort | 2 | 0 | u8 (child<<4 \| port), u8 reg | |
| D8 | cmdRegLoad | 2 | 0x04 | u8 reg, u16 value | `writeReg(reg, value)` |
| D9 | cmdReg | 3 | 0x30 | u8 op, u8 reg, **reg** src | arithmetic, see below |
| DA | cmdReg | 3 | 0x10 | u8 op, u8 reg, u16 imm | arithmetic, see below |
| DB | cmdRegUni | 2 | 0 | u8 op(11=neg), u8 reg | `reg = -reg` |
| DC | cmdRegTblLoad | 4 | 0xE0 | u8 op, u8 reg, u24 table, **reg** idx | op 12: u8[idx], 13: u16[idx], 14: u24[idx], 15: u32[idx], 16: u32 @ table+idx |
| E0 | cmdTempo | 1 | 0x01 | u16 bpm | `setTempo` |
| E1 | cmdBankPrg | 1 | 0x01 | u16 (bank<<8 \| prog) | |
| E2 | cmdBank | 1 | 0 | u8 bank | |
| E3 | cmdPrg | 1 | 0 | u8 program | ≥0xF0 → `noteOnOsc` (built-in oscillator) |
| E7 | cmdEnvScaleSet | 2 | 0x04 | u8 osc, u16 s | `mOscParam[osc].mScale = s/16383` |
| E8 | cmdEnvSet | 2 | 0x08 | u8 osc, u24 offset | track envelope table = Points at `base+offset` (overrides the instrument osc, see 3.4) |
| E9 | cmdSimpleADSR | 5 | 0x155 | 5×u16: A, D, S-time, S-level, release | see 3.4 |
| EA | cmdBusConnect | 2 | 0x04 | u8 line, u16 cfg | mixer bus routing (`mMixConfig[line]`) |
| EB | cmdIIRCutOff | 1 | 0 | u8 idx(<128) | `setIIR(CUTOFF_TO_IIR_TABLE[idx])` |
| EC | cmdIIRSet | 4 | 0x55 | 4×u16 | IIR coefs |
| ED | cmdFIRSet | 1 | 0x02 | u24 offset | 8 s16 FIR coefs at `base+offset` |
| F0 | cmdWait | 0 | – | **VLQ** | wait N ticks |
| F1 | cmdWaitByte | 1 | 0 | u8 | wait N ticks |
| F3 | cmdSetIntTable | 1 | 0x02 | u24 | interrupt vector table (3-byte entries) |
| F4 | cmdSetInterrupt | 1 | 0x01 | u16 mask | enable interrupts |
| F5 | cmdDisInterrupt | 1 | 0x01 | u16 mask | disable interrupts |
| F6 | cmdRetI | 0 | – | – | return from interrupt |
| F7 | cmdClrI | 0 | – | – | clear interrupt |
| F8 | cmdIntTimer | 2 | 0x04 | u8 count, u16 ticks | timer interrupt (type 5) |
| F9 | cmdSyncCPU | 1 | 0x01 | u16 | REG_F = game callback(value) or 0xFFFF |
| FD | cmdPrintf | 0 | – | NUL-terminated string, then 1 byte per `%` arg | debug print |
| FE | cmdNop | 0 | – | – | |
| FF | cmdFinish | 0 | – | – | returns −1: the track ends and is closed |

**Opcodes with NULL handlers.** `A0–B0` (except B0's extended path), `B5–B7`, `BC–C0`, `CD–CF`, `DD–DF`, `E4–E6`, `EE–EF`, `F2` and `FA–FC` have NULL handlers and `numArgs=0`. They consume only the opcode byte.

**`cmdReg` ops** (`args = {op, reg, val}`; the result is written with `writeReg(reg, val)`):

| op | Result |
|---|---|
| 0 | val (plain load) |
| 1 | val + reg |
| 2 | reg − val |
| 3 | reg − val, written to REG_F only (compare) |
| 4 | val × reg, written to reg 0x21 (= regs 4,5 as a 32-bit pair) |
| 5 | val & reg |
| 6 | val \| reg |
| 7 | val ^ reg |
| 8 | random % val |
| 9 | reg << val |
| 10 | reg >> val |

**Branch conditions** (`conditionCheck`). The value tested is REG_F (register 3, u16):

| cond | True when |
|---|---|
| 0 | always |
| 1 | REG_F == 0 |
| 2 | REG_F != 0 |
| 3 | REG_F == 1 |
| 4 | REG_F ≥ 0x8000 (negative) |
| 5 | REG_F < 0x8000 |

**Registers** (`readReg` / `writeReg`). Every `writeReg` also copies the value into REG_F (3).

| Register | Meaning |
|---|---|
| 0..13 | `JASRegisterParam` u16 regs. Names: 0=A, 1=B, 2=S, 3=F, 4=X, 5=Y. Writes to 0–2 are truncated to u8 |
| 32 | A:B as hi/lo bytes |
| 33 | X:Y as a 32-bit pair |
| 34..41 | further byte and halfword views (`JASRegisterParam::write`) |
| 0x40–0x4F | track ports 0–15 |
| 0x60 | (read-only) bitmask of running child tracks |
| 0x61 | (read-only) current loop counter |
| 0x62 | **timebase** |
| 0x63 | transpose |
| 0x64 | bendSense (pitch-bend range) |
| 0x65 | gateRate (%) |
| 0x66 | skipSample |
| 0x67 | bank |
| 0x68 | program |
| 0x69 | panPower × 32767 |
| 0x6A | releasePrio<<8 \| noteOnPrio |
| 0x6B | noteOnPrio |
| 0x6C | releasePrio |
| 0x6D | directRelease |
| 0x6E | vibDepth × 1524 |
| 0x6F | vibDepth × 12192 |
| 0x70 | tremDepth × 256 |
| 0x71 | vibPitch × 64 |
| 0x72 | tremPitch × 64 |
| 0x73 | vibDelay |
| 0x74 | tremDelay |

To set the timebase, use `D8 62 hi lo` (RegLoad).

**Addresses.** All u24 addresses are offsets from the sequence **base**. For an SE this is the start of the **BSC file**, not the start of the SE's own data (see 6.3). Reads are big-endian (`read16`, `read24`). `get24(off)` reads the u24 at `base+off`.

**VLQ** (`JASSeqReader::readMidiValue`):

- A MIDI-style varint of 7 bits per byte, MSB first.
- Bit 7 set means another byte follows.
- At most 4 bytes; if there would be more, the warning "Too large value" is issued and 0 is returned.

**Loops.** `loopStart(n)` pushes the current position and `n`. `loopEnd` handles the counter like this:

```cpp
u16 tmp = mLoopCount[depth-1]; if (tmp != 0) tmp--;
if (!tmp) { depth--; return; }                // done
mLoopCount[depth-1] = tmp; jump(returnAddr);   // repeat
```

- The body runs `n` times in total.
- `n = 0` or `n = 1` both run the body once; 0 is **not** "infinite".
- The call stack and loop stack are shared, with depth 8 (`JAS_SEQ_STACK_SIZE`).

### 1.6 Timed parameters (B8–BB)

`JASTrack::setParam(id, target, count)`:

- `count == 0` sets the value immediately.
- Otherwise the value ramps once per **tick** (in `JASTrack::tickProc` → `updateTimedParam`, after the sequence tick):

```cpp
if (param->mCount != 0) { param->mValue += (param->mTarget - param->mValue) / param->mCount; param->mCount--; }
```

| id | Param | Default | Effect (`JASTrack::updateTrack`) |
|---|---|---|---|
| 0 | volume | 1.0 | channel volume factor = **volume²** |
| 1 | pitch | 0.0 | octave offset = `pitch × bendSense / 3` (bendSense default 12, so ±1.0 ≈ ±4 octaves). Added in the `pow2` exponent |
| 2 | fxmix | 0.0 | reverb send (additive) |
| 3 | pan | 0.5 | `(pan−0.5)·panPower` + 0.5, 0..1 |
| 4 | dolby | 0.0 | surround (additive) |
| 5 | distFilter | 0.0 | DSP distance filter `×32767` |

Child-track values combine with the parent's: volume and pitch multiply; pan, fx and dolby add, and pitch octaves add.

The pitch formula is verbatim in both decomps:

```cpp
f32 fra = mMoveParam.array[1].mValue * mBendSense * 0.333333343267f;  // PETARI JASTrack.cpp:418
```

**8-bit value expansion** (B8/BA, identical in both decomps):

```cpp
s16 v = arg << 8;  if ((arg & 0x80) == 0) v |= (s16)(arg << 1);   value = v / 32767.0f;
```

`0x7F` → 0.99994, `0x40` → 0.504, `0x00` → 0, `0x80` → −1.0, `0xFF` → −0.0078. The byte is effectively an **s8**.

### 1.7 Tempo, timebase, tick clock

**Defaults** (`JASTrack::init`, PETARI JASTrack.cpp:101):

| Setting | Default |
|---|---|
| `mTempo` | 120 |
| `mTimebase` | 48 (ticks per beat) |
| `mBendSense` | 12 |
| `mGateRate` | 100 |
| `mProgNumber` | 0xF0 |
| `mBankNumber` | 0 |
| `mNoteOnPrio` | 0x40 |
| `mTranspose` | 0 |
| latest key | 60 |

**Clock.**

- **DAC rate.** `JASDriver::setOutputRate(OUTPUT_RATE_0)`: `sDacRate = 32000 * 1.0008897 = 32028.47 Hz`, `sSubFrames = 7`.
- **Sub-frame.** A sub-frame is **0x50 = 80 samples** (`getFrameSamples = sSubFrames*0x50`).
- **Per sub-frame work.** `JASDriver::updateDSP` runs once per sub-frame. It calls `JASDSPChannel::updateAll()`, which updates every channel (envelopes, timers, pitch, volume). It then calls `subframeCallback()`, which runs `JASTrack::TList::seqMain`. That is about **400.36 updates/s**.
- **Sequence ticks** (`JASTrack::seqMain` / `updateTempo`):

```cpp
mSampleInterval = mTempoRate * (4/3 * mTimebase * mTempo / dacRate);   // ticks per sub-frame
while (acc >= 1.0f) { acc -= 1.0f; tickProc(); }  acc += mSampleInterval;   // acc starts at 1.0
```

- **Tick rate.** `ticks/s = (dacRate/80) · (4/3)·tb·tempo/dacRate = tb·tempo/60`. So one beat = `timebase` ticks, and the defaults give 96 ticks/s.
- **Wait semantics.** `wait(n)` consumes exactly n ticks. In `JASSeqCtrl::tickProc`, if the counter is > 0 it is decremented, then commands are parsed while the counter == 0.
- **Child tracks.** A child copies the root's tempo and timebase. Children are ticked after their parent in the same tick, including a child opened during that tick.
- **Gate length in sub-frames.** `seqTimeToDspTime(t) = t / mSampleInterval`, since `mIsDirectlyPlayed` is true by default.

---

## 2. IBNK version 1 (chunked bank)

### 2.1 Header and chunk walk (PETARI JASBNKParser.cpp, TP JASBNKParser.h)

```
0x00 'IBNK'
0x04 u32 size
0x08 u32 bank number (global id; registered in JASDefaultBankTable[bankNo])  (JASBNKParser::getBankNumber = u32[2])
0x0C u32 version   (0 = old 'BANK' layout, 1 = chunked)
0x20 first chunk
```

```cpp
TChunk* chunk = stream + 0x20;
while (chunk < stream + size) { if (chunk->mID == id) return chunk;
    chunk = (chunk + 0xb + chunk->mSize) & ~3; }   // = align4(chunk + 8 + size)
```

- Each chunk is `u32 id; u32 size; data[size]`.
- The SMG parser uses `ENVT`, `OSCT` and `LIST`. `RAND`, `SENS`, `INST`, `PERC` and `PMAP` are only containers: their records are reached through **absolute offsets** (from the IBNK start) stored in LIST, Inst and Pmap records.

### 2.2 ENVT

`ENVT, size, data`. The data is a pool of `JASOscillator::Point` arrays:

```c
struct Point { s16 mode; s16 time; s16 value; };   // 6 bytes; mode/time/value per 3.x
```

The parser copies `ENVT+8 … +size` verbatim. Osc envelope offsets are relative to **ENVT+8**.

### 2.3 OSCT

The layout is `OSCT, size, u32 count`, followed by `count` records of 0x1C bytes:

```
0x00 'Osci'
0x04 u8  target (0 vol,1 pitch,2 pan,3 fxmix,4 dolby,5 vibrato-depth mult,6 tremolo-depth mult) + 3 pad
0x08 f32 rate          (counter speed multiplier)
0x0C u32 attack table offset  (rel. ENVT+8)
0x10 u32 release table offset (rel. ENVT+8)
0x14 f32 scale  (width)
0x18 f32 vertex (offset)
```

`data->mTable = envt + op->mTableOffset; data->rel_table = envt + op->_10;`. An offset of 0 still points at ENVT+8; the parser never produces NULL in Ver1.

### 2.4 LIST

The layout is `LIST, size, u32 count, u32 offsets[count]`.

- `offsets[i]` is absolute from the IBNK start; 0 means empty.
- `i` is the **program number**: `bank->setInst(i, ...)`. Programs ≥ 0xF0 never reach the bank (oscillator voices).
- The record at the offset starts with `'Inst'` or `'Perc'`.

### 2.5 `Inst` record (SMG, PETARI `Ver1::createBasicBank`)

```
'Inst'
u32 oscCount;            u32 oscIndex[oscCount];          // index into OSCT records (max 2: osc 0, osc 1)
u32 effectCount;         u32 effectOffset[effectCount];   // absolute offsets of 'Rand'/'Sens' records
u32 keyRegionCount;
  keyRegion[keyRegionCount]:
    u8  highKey; u8 pad[3];
    u32 veloRegionCount;
    veloRegion[veloRegionCount]  (16 bytes each):
        0x00 u8  highVelocity; pad[3]
        0x04 u32 wave: high u16 = wsys/group id (ignored by runtime), low u16 = wave id
        0x08 f32 volume
        0x0C f32 pitch
f32 instVolume;
f32 instPitch;
```

```cpp
keymap->setHighKey(data[0] >> 0x18);  u32 veloCount = data[1];  data += 2;
for k: velomap->_0 = veloData->_0 /*u8 highVel*/; velomap->_4 = (u16)veloData->_4;
       velomap->mVolume = veloData->mVolume; velomap->mPitch = veloData->mPitch; data += 4;
TInstData* instData = (TInstData*)data; instp->setVolume(instData->mVolume); instp->setPitch(instData->mPitch);
```

### 2.6 `Perc` record and `Pmap`

```
'Perc'
u32 count  (≤128; index = key)
u32 pmapOffset[count]   // absolute; 0 = no sound on this key
```

Each `Pmap` record at its offset:

```
0x00 'Pmap'
0x04 f32 volume
0x08 f32 pitch
0x0C u8  pan (0..127, /127 -> 0..1)  ; pad
0x0E u16 release  (direct-release word, see 3.3; TPerc default 1000)
0x10 u32 effectCount; u32 effectOffset[effectCount]
     u32 veloRegionCount; veloRegion[...] (same 16-byte layout as Inst)
```

### 2.7 `Rand` / `Sens` effect records (`Ver1::createEffector`; offsets are absolute)

```
'Rand' 0x04 u8 type (0 vol,1 pitch,2 pan,3 fxmix,4 dolby); 0x08 f32 base; 0x0C f32 range
'Sens' 0x04 u8 type; 0x05 u8 senseType (1 = velocity, 2 = key); 0x06 u8 maxPoint; 0x08 f32 startLvl; 0x0C f32 endLvl
```

- **Rand** (`JASInstRand::effect`):
  - `r = (2*rand01 - 0.9999999)*range + base`.
  - For vol and pitch the parameter is multiplied by `r`; for pan add `r - 0.5`; for fx and dolby add `r`.
- **Sens** (`JASInstSense::effect`), with `val` = velocity or key:
  - If `maxPoint` is 0 or 127: `start + val*(end-start)/127`.
  - Else if `val < maxPoint`: `start + (1-start)*val/maxPoint`.
  - Else: `1 + (end-1)*(val-maxPoint)/(127-maxPoint)`.
  - The result is applied like Rand.
- **Order.** Effects are applied **before** the key/velocity-region factors. Rand changes on every note-on, so offline rendering is non-deterministic unless you seed it or use `base`.

### 2.8 Region selection (`JASBasicInst::getParam`, PETARI JASBasicInst.cpp)

```cpp
vol = instVolume; pitch = instPitch;  apply effects;
keyMap  = first region with key      <= highKey     (else: no sound)
veloMap = first region in it with vel <= highVelocity (else: no sound)
vol *= veloMap.volume; pitch *= veloMap.pitch; waveId = veloMap.wave & 0xFFFF;
oscillators = inst osc[0], osc[1] (count 2, NULL slots are "not valid")
```

The key region has no volume or pitch of its own; only the velocity region does.

**Drums** (`JASDrumSet::getParam`):

- `key < 128` indexes the Perc entry.
- Volume, pitch and pan come from the Pmap, and directRelease = the Pmap release. Effects and the velocity region are then applied as for instruments.
- A single oscillator with **NULL table** is used: the oscillator holds at 1.0 until the direct release.
- `_24 = true`, so `setKey` is **not** called: drums play at the wave's own pitch (key offset 0) times Pmap pitch times velocity-region pitch.

---

## 3. JASOscillator (envelopes)

The source is identical in PETARI and TP `JASOscillator.cpp`. Fields:

| Field | Meaning |
|---|---|
| `_04` | time counter |
| `_08` | current value |
| `_0C` | segment start value |
| `_10` | segment target value |
| `mCurPoint` (`_14`) | current point index |
| `mEnvelopeMode` (`_18`) | curve 0..3 |
| `_1C` | state: 0 = stop, 1 = attack table, 2 = hold/sustain, 3 = release table, 4 = direct release |

### 3.1 Point semantics

Each `Point {mode, time, value}` describes **one segment**: ramp from the current value to `value/32768` over `time` counter units, using curve `mode`. Special modes:

| mode | Meaning |
|---|---|
| 0 | linear (`sCurveTableLinear`) |
| 1 | `sCurveTableSquare` (values 1, .968, .935 … .25, 0; slow start) |
| 2 | `sCurveTableSqRoot` (1, .879, .766 … .0039, 0; fast start) |
| 3 | `sCurveTableSampleCell` (1, .970, .781, .546 … 0; exponential-like) |
| 0xD (13) | **loop**: `mCurPoint = value` (index) |
| 0xE (14) | **hold**: state 2, value frozen (sustain) |
| 0xF (15) | **stop**: state 0. For osc 0 this kills the channel |

The Ver0 table end is the first `mode > 10`.

```cpp
void JASOscillator::initStart(const Data* data) {
    _04=_08=_0C=0; mCurPoint=0; mDirectRelease=0;
    if (!data) { _1C = 0; return; }
    if (data->mTable == NULL) { _1C = 2; _08 = 1.0f; return; }    // no table: constant 1.0
    _10 = mTable[0].value / 32768.0f; mEnvelopeMode = mTable[0].mode; _1C = 1;  // starts from 0.0
}
void JASOscillator::incCounter(f32 inc) {             // called once per sub-frame (see 3.2)
    if (_1C == 0 || _1C == 2) return;                 // states 1,3,4 advance
    _04 += inc * mData->mRate; update();
}
void JASOscillator::update() {
    if (_1C == 4) { s32 x = mDirectRelease & 0x3FFF;
        if (_04 < x) updateCurrentValue(x); else { _08 = _10; _1C = 0; } return; }
    if (_1C == 2) return;
    const Point* t = (_1C == 1) ? mData->mTable : mData->rel_table;
    if (!t) { _1C = 2; return; }
    while (_04 >= t[mCurPoint].time) {
        _04 -= t[mCurPoint].time; _08 = _10; mCurPoint++; _0C = _08;
        const Point* ps = &t[mCurPoint];
        switch (ps->mode) {
        case 0xF: _1C = 0; return;
        case 0xE: _1C = 2; return;
        case 0xD: mCurPoint = ps->value; break;           // NOTE: _10 not reloaded
        default:  mEnvelopeMode = ps->mode; _10 = ps->value / 32768.0f; break;
        }
    }
    updateCurrentValue(t[mCurPoint].time);
}
void JASOscillator::updateCurrentValue(f32 len) {
    f32* table = table_list[mEnvelopeMode];               // {Linear, Square, SqRoot, SampleCell}
    f32 x = 16.0f * (_04 / len); u32 i = (u32)x; f32 f = x - i;
    f32 c = (1-f)*table[i] + f*table[i+1];                // 17-entry table, 1.0 -> 0.0
    _08 = _0C * c + _10 * (1.0f - c);
}
f32 JASOscillator::getValue() const { return _08 * mData->mScale + mData->mVertex; }
```

**Loop quirk.** On `0xD` the target `_10` stays the old one. The looped-to point therefore runs as a flat segment, with the old mode, for that point's time. After it, the next point loads a new target.

**Zero-time points.** Points with time 0 complete instantly; the `while` keeps going within the same update.

**Release** (`JASOscillator::release`, called by `JASChannel::release`):

```cpp
if (rel_table == NULL && mDirectRelease == 0) { stop(); return; }          // osc0 -> channel dies now
if (mDirectRelease != 0) { _04=0; _0C=_08; _10=0; mCurPoint=0;
    mEnvelopeMode = (mDirectRelease >> 14) & 3; _1C = 4; update(); return; }
if (mTable != rel_table) { _04=0; _0C=_08; _10 = rel_table[0].value/32768; mCurPoint=0;
    mEnvelopeMode = rel_table[0].mode; }
_1C = 3; update();
```

- **Direct release word.** `u16 = (curve<<14) | time(14 bits)` ramps from the current value to 0.
- **Precedence.** A non-zero directRelease **wins over the release table**.
- **Sources.** The direct release comes from the drum Pmap release, from the track's `mDirectRelease` (register 0x6D or the 5th argument of `E9`), or from `noteOff(id, release)`.
- **Same table for both.** If `rel_table == mTable`, the release continues from the current point of the same table.

### 3.2 Time units

Per sub-frame (`JASChannel::updateDSPChannel`), `inc = 48000.0f / JASDriver::getDacRate()`, about 1.4987. There are `dacRate/80` sub-frames per second, so **the counter advances 48000/80 = 600 units per second × `rate`**. An envelope `time` of 600 is therefore 1 s at `rate` 1.0.

Vibrato and tremolo LFOs use `inc = 32028.5/dacRate`, about 1 per sub-frame:

- Phase advances by `inc*(1/72)*lfoPitch` per sub-frame, giving about 5.56 Hz × vibPitch.
- The value is `depth*sin(2π·phase)`, after a delay of `vibDelay` sub-frames.

### 3.3 How oscillator values are applied (`JASChannel::effectOsc`)

| target | Application |
|---|---|
| 0 volume | `EffectParams.mVolume *= v` |
| 1 pitch | `mPitch *= v` (a **ratio** multiplier, not semitones) |
| 2 pan | `mPan += v - 0.5` |
| 3 fx | `mFxMix += v` |
| 4 dolby | `mDolby += v` |
| 5 | multiplies vibrato depth |
| 6 | multiplies tremolo depth |

- **Channel death.** When **osc 0** reaches state 0 (stop), the channel is freed (`if (i == 0 && isStop())`). This is how release ends a note. A non-looped sample also ends the channel when the DSP reaches its end.
- **Holding notes.** An osc in state 2 (hold or no table) never ends the channel by itself.

### 3.4 Track-level envelope override (E8 / E9)

- **When it applies.** In `JASTrack::overwriteOsc`, if `mOscParam[i].mTable != NULL` the channel's osc `i` uses the track's Data instead of the instrument's. By default the table is NULL, so there is no override.
- **E8 EnvSet.** Sets the table to Points inside the sequence data.
- **E9 SimpleADSR(A, D, S, L, R).** Builds the table from `sAdsTable`:

```
{0, A, 0x7fff}   linear 0 -> 1.0 over A
{0, D, 0x7fff}   stay 1.0 for D
{0, S, L}        linear to L/32768 over S
{0xE, 0, 0}      hold
```

  Its `rel_table` is NULL; `R` becomes the direct-release word.
- **Track oscillator defaults.** `sEnvOsc = {target 0, rate 1, …, scale 1, vertex 0}`; osc 1 uses target 1 (pitch).

---

## 4. Final pitch and volume of a channel

### 4.1 Note-on (`JASBank::noteOn`, PETARI JASBank.cpp)

```cpp
if (progNo >= 0xf0) return noteOnOsc(...);                    // built-in osc voice, ignore for SE banks
getInstParam(prog, key, vel, &p);  wave = waveBank->getWaveHandle(p._18 /* u16 wave id */);
channel->setInitPitch(p.mPitch * (waveInfo->mSampleRate / JASDriver::getDacRate()));
if (!p._24) channel->setKey(key - waveInfo->mBaseKey);        // drums: key stays 0
channel->setInitVolume(p.mVolume);  channel->setVelocity(vel);
channel->setInitPan(p.mPan); channel->setInitFxmix(p.mFxMix); channel->setInitDolby(p.mDolby);
for (i < p.mNumOscillators) channel->setOscInit(i, p.mOscillatorData[i]);
channel->setDirectRelease(p.mDirectRelease);
```

Here `key` already includes the track transpose.

### 4.2 Per update (`JASChannel::updateDSPChannel`; identical code in TP)

```cpp
f32 pitch = JASCalc::pow2(mParams._8 /*track pitch octaves*/ + (mKey + mKeySweep) / 12.0f
                          + effect_params._14 * mVibrate.getValue());
pitch = mSoundParams.mPitch /*instPitch*veloPitch*SR/dac*/ * effect_params.mPitch /*osc*/
        * pitch * mParams.mPitch /*track × JAISound pitch*/ * 4096.0f;
i_channel->setPitch(pitch);                         // u16, clamped to 0x7fff; 4096 = 1 input sample per output sample

f32 volume = mVelocity / 127.0f;  volume *= volume;               // velocity curve = (vel/127)^2
volume *= mSoundParams.mVolume /*instVol*veloVol*effects*/ * i_params.mVolume /*osc vol*/
        * mParams.mVolume /*track volume^2 × parents × sound (SE) volume*/
        * (i_params._18 * mTremolo.getValue() + 1.0f);
```

So for offline rendering at output rate `R`, the source sample step per output sample is:

```
step = waveSR/R * instPitch * veloPitch * oscPitch * soundPitch
       * 2^( (key + transpose - baseKey + keySweep)/12 + trackPitchParam*bendSense/3 + vib )
```

`getDacRate` cancels out. The DSP pitch register caps at 0x7FFF (step < 8.0 at the DAC rate).

**Final amplitude:**

```
vol = (vel/127)^2 * instVol * veloVol * randSens * oscVol(t) * trackVol^2 * parentTrackVols * seVolume * (1 + trem)
```

It is clamped to ≥ 0 and later to [0, 1] per bus.

**Pan.** `pan = clamp01(0.5 + (inst.pan−0.5) + (osc.pan−0.5) + (track.pan−0.5))`. For mixing:

- The default mix configs are `0x150, 0x210, 0x352, 0x412, 0, 0` (`MixConfig = {u8 bus; u4 m0; u4 m1}`, see `JASChannel::updateMixer`).
- Bus 1 gets `vol·sin((1−pan)·π/2)` and bus 2 gets `vol·sin(pan·π/2)`. This is equal-power pan in stereo; mono uses the linear scale.
- Buses 3 and 4 are the same multiplied by fxmix (reverb sends).
- Bus 1 = main L, 2 = main R, 3/4 = FX L/R is **inferred** from these configs and is not confirmed against the DSP microcode.

**Interpolation.** The DSP resamples with a 4-tap polyphase filter (`JASDsp::DSPRES_FILTER`, 320 words in JASDSPInterface.cpp). Linear or cubic interpolation offline is an approximation.

---

## 5. WSYS and wave data

### 5.1 WSYS layout (PETARI `JASWSParser.hpp`; matches JSX)

```
WSYS header
0x00 'WSYS'
0x04 u32 size
0x08 u32 id            (= 'ws ' id in the BAA; the bank's wave bank number)
0x0C u32 waveTableSize (TP uses it; SMG recomputes max id+1)
0x10 u32 offset -> WINF
0x14 u32 offset -> WBCT

WINF: 'WINF', u32 groupCount, u32 offset[groupCount] -> WaveGroup (TWaveArchive)

WaveGroup (TWaveArchive):
0x00 char awFileName[0x70]
0x70 u32  waveCount
0x74 u32  offset[waveCount] -> Wave

Wave (TWave), 0x24 bytes:
0x00 u8  unknown
0x01 u8  format       (0 AFC 4-bit/9-byte frames, 1 AFC 2-bit/5-byte frames, 2 PCM8, 3 PCM16)
0x02 u8  baseKey      (root MIDI key)
0x03 u8  pad
0x04 f32 sampleRate
0x08 u32 awOffset     (byte offset of the data inside awFileName)
0x0C u32 awLength     (bytes)
0x10 u32 loopFlag     (0 = one-shot; nonzero -> looped; the parser stores 0 or 0xFF)
0x14 u32 loopStart    (sample)
0x18 u32 loopEnd      (sample)
0x1C u32 sampleCount
0x20 s16 loopPredictorLast   (mpLast)
0x22 s16 loopPredictorPenult (mpPenult)

WBCT: 'WBCT', u32 unknown, u32 groupCount, u32 offset[groupCount] -> SCNE   (parallel to WINF)
SCNE: 'SCNE', 8 bytes, 0x0C u32 offset -> C-DF  (JSX: 0x10 C-EX, 0x14 C-ST, unused)
C-DF: 'C-DF', u32 count, u32 offset[count] -> entry { u16 awId/group; u16 waveId }  (parallel to the group's waves)
```

**Wave lookup.** `wave_bank->setWaveInfo(group, j, JSULoHalf(ctrlWave->mData), info)`: wave `j` of group `i` gets **wave id = the low u16 of its C-DF entry**. The instrument velocity region's `wave & 0xFFFF` is that id, looked up in the WSYS bound to the bank (see 7). The high halves are ignored at runtime.

### 5.2 DSP block geometry (`JASDsp::TChannel::setWaveInfo`)

```cpp
static const u8 COMP_BLOCKSAMPLES[8] = {0x10, 0x10, 0x01, 0x01, 0x01, 0x10, 0x10, 0x01};
static const u8 COMP_BLOCKBYTES[8]   = {0x09, 0x05, 0x08, 0x10, 0x01, 0x01, 0x01, 0x01};
// looped:   playback runs to loopEnd, then continues at loopStart, with predictor history mpLast/mpPenult
// one-shot: end = sampleCount
```

| Format | Block | Bytes per sample |
|---|---|---|
| 0 | 16 samples / 9 bytes | 0.5625 |
| 1 | 16 samples / 5 bytes | 0.3125 |
| 2 | PCM8 | 1 |
| 3 | PCM16 | 2 |

For PCM, "BLOCKBYTES" holds bits per sample.

### 5.3 AFC ADPCM decoding

**Coefficients.** The coefficient table is `JASDsp::DSPADPCM_FILTER[64]` (PETARI JASDSPInterface.cpp:74), 16 big-endian s16 pairs. It equals vgmstream's `afc_coefs`:

```
{0,0},{2048,0},{0,2048},{1024,1024},{4096,-2048},{3584,-1536},{3072,-1024},{4608,-2560},
{4200,-2248},{4800,-2300},{5120,-3072},{2048,-2048},{1024,-1024},{-1024,1024},{-1024,0},{-2048,0}
```

The decoding itself runs in DSP microcode, which is not decompiled. The algorithm below is vgmstream's (VGM `decode_afc` / `decode_afc_2bit`):

```c
// 4-bit, 9-byte frame -> 16 samples
scale = 1 << (frame[0] >> 4);  idx = frame[0] & 0xF;  c1 = coef[idx][0]; c2 = coef[idx][1];
for i in 0..15:
    n = nibble(frame[1 + i/2], high nibble first), sign-extended 4 bits (-8..7)
    s = ((n * scale) << 11);
    s = (s + c1*hist1 + c2*hist2) >> 11;   s = clamp16(s);
    hist2 = hist1; hist1 = s;

// 2-bit, 5-byte frame -> 16 samples
scale = 8192 << (frame[0] >> 4);  (same idx/coefs)
n = 2-bit field of frame[1 + i/4], MSB pair first (shifts 6,4,2,0), mapped {0,1,-2,-1}
s = (n*scale + c1*hist1 + c2*hist2) >> 11; clamp16
```

- History starts at 0 and carries across frames.
- PCM16 is big-endian s16. PCM8 is assumed signed 8-bit; this is **unverified**.

**Looping.**

- **Linear decode is equivalent.** Because `mpLast`/`mpPenult` are the encoder's predictor state at `loopStart` (`hist1`, `hist2`), decoding the whole wave linearly once and looping in the **PCM domain** (`loopStart`..`loopEnd`) gives the same result.
- **Inferred.** Treating `mpLast` as hist1 and `mpPenult` as hist2 at re-entry is inferred from the naming and the DSP channel fields (`_104`, `_106`).
- **Unconfirmed: end bound.** Whether `loopEnd` is exclusive or inclusive is not confirmed. Use `loopEnd` as the exclusive end, i.e. play sample `loopEnd-1` and then jump.

---

## 6. BST / BSTN / BSC and sound IDs

### 6.1 JAISoundID (u32, PETARI JAISound.hpp)

```
byte 0: sectionID   (0 = SE, 1 = sequence/BGM, 2 = stream   -- JAUStdSoundInfo::getSoundType)
byte 1: groupID     (for SE = SE category; JAUStdSoundInfo::getCategory)
u16   : index ("waveID" in the decomp naming) inside the group
0xFFFFFFFF = anonymous
```

### 6.2 BST (JAUSoundTable) and BSTN (JAUSoundNameTable)

```
BST
0x00 'BST '   (TP checks u32+0xBDAD0000 == 0x5420, i.e. 'BST ')
0x0C u32 offset -> Root
Root:    u32 sectionCount; u32 sectionOffset[sectionCount]         (absolute; 0 = none)
Section: u32 groupCount;   u32 groupOffset[groupCount]
Group:   u32 itemCount; u32 (unknown, "mGlobalOffset"); u32 itemWord[itemCount]
         itemWord: top byte = typeID, low 24 bits = absolute offset of the item
```

```cpp
u8  getTypeID(i)    { return ((u8*)mItemOffsets)[i*4]; }
u32 getItemOffset(i){ return mItemOffsets[i] & 0xffffff; }
```

**Type IDs.** `typeID & 0xF0`: `0x50` SE, `0x60` BGM sequence, `0x70` stream (`JAUSoundInfo::DATA_SE/BGM/STREAM`). `typeID & 0x40` is true for all three and enables priority and volume. The low nibble's meaning is **unknown** (TP's game code tests for exactly 0x51 for its extended SE layout).

**SE item** (`JAUSoundTableSe`, used by SMG's `JAUStdSoundInfo` and `AudSoundInfo`):

```
0x00 u8  priority      (JAISeMgr voice/stealing priority)
0x01 u8  volume        -> JAISound property volume = volume / 255.0  (JAUStdSoundInfo::getSoundInfo_)
0x02 u16 audibleSw     (3D audible switches; JAUAudible)
0x04 u16 soundSw       (AudSoundInfo):
                          bits 3-4  max simultaneous instances (0 = unlimited)
                          bits 5-6  min-count limit class: 0x20 -> 1, 0x40 -> 5, 0x60 -> 10
                          bit 8     fanfare SE
                          bit 9     use FX mix
                          bit 10    use LPF
```

- **Volume divisor.** TP's Z2 divides the volume byte by 127; SMG's JAUStdSoundInfo divides by **255**.
- **BGM item.** `{u8 prio, u8 vol, u16 resourceId (seq resource in the seq .arc), u16 chordResId, u16 soundSw}`; soundSw bit 0 marks a rhythm BGM.
- **Stream item.** `{u8 prio, u8 vol, u16 channelCtrl (2 bits per child: 1 center, 2 left, 3 right), u32 fileNameOffset (absolute)}`.

**BSTN.** `'BSTN'`, with the Root offset at 0x0C and the same Root layout. According to the PETARI header the deeper levels differ from BST:

```
Section: u32 groupCount; u32 (unknown); u32 groupOffset[...]
Group:   u32 itemCount;  u32 categoryNameOffset; u32 nameOffset[itemCount]   // absolute -> C string
```

`getName(id)` follows section → group → item exactly like BST. **Unverified:** the extra word in the BSTN *section* comes only from the Petari header and was not checked against data. Confirm it against a real file; if names come out shifted, drop it.

### 6.3 BSC (JAUSeqCollection): SE sequence data

```
0x00 'S','C'
0x02 u16 groupCount
0x04 u32 fileSize
0x08 u32 tableOffset[groupCount]     -> table: u32 count; u32 seqOffset[count]   (all absolute from BSC start)
```

```cpp
bool JAUSeqCollection::getSeqData(int groupId, int waveId, JAISeqData* p) {
    if (groupId >= mGroupNum) return false;
    JAUSeqCollectionSoundTable* t = (u8*)mCollectionData + mSoundTableOffsets[groupId];
    if (waveId >= t->mWaveNum) return false;
    p->set((void*)mCollectionData, t->mWaveSeqOffsets[waveId]);   // base = BSC start, offset = entry
}
// JAUSeqDataMgr_SeqCollection::getSeqData(id) -> getSeqData(id.groupID, id.index)
```

**Mapping.** SE `JAISoundID (0, g, i)` maps to:

- name: BSTN section 0, group g, item i;
- table entry: BST section 0, group g, item i;
- code: the BSC entry `[g][i]`.

**Playback** (`JAISe::startTrack_`):

- `track.setChannelMgrCount(1); assignExtBuffer(0, &soundParams); setSeqData(BSC base, offset); startSeq()`.
- The root track starts at that offset with the defaults from 1.7, and jumps and opens are relative to the BSC start.
- The SE track uses `JASDefaultBankTable`, indexed by the bank number from IBNK+8. The bank-list exception is described in section 7.
- The SE ends when the root track hits `FF` (cmdFinish) and its children have stopped.

---

## 7. BAA container (`JAUAudioArcInterpreter::readCommand_`, PETARI and TP identical)

The file starts with `'AA_<'`. After that comes a stream of commands, each with u32 arguments, until `'>_AA'`. Offsets are from the BAA start.

| Tag | Args | Loader action (`JAUAudioArcLoader`, `JAUSection`) |
|---|---|---|
| `'ws  '` | u32 wsysNo, u32 offset, u32 loadMask | `newWaveBank(wsysNo, WSYS@offset)`; if `loadMask` is set, load the wave groups whose bit `i` is set |
| `'bnk '` | u32 wsysNo, u32 offset | `newBank(IBNK@offset, wsysNo)`. Bank number = IBNK u32 @+8. The bank is bound to wave bank `wsysNo` (`assignWaveBank`) and registered in `JASDefaultBankTable` (or in the open bank list) |
| `'bl_<'` | u32 a, u32 count | `beginNewBankTable(a, count)`: the following `bnk` entries go into this list |
| `'>_bl'` | – | `endNewBankTable()` |
| `'bsc '` | u32 start, u32 end | SE sequence collection (`newSeSeqCollection`), size = end−start |
| `'bst '` | u32 start, u32 end | sound table |
| `'bstn'` | u32 start, u32 end | sound name table |
| `'bms '` | u32 soundId, u32 start, u32 end | static sequence for that JAISoundID (JSX's "leading 01" is section 1 = sequence) |
| `'bmsa'` | u32 soundId | static sequence loaded from the seq archive, using BST `resourceId` |
| `'vbnk'` | u32 a, u32 b | `newVoiceBank(bank=b, wsys=a)` |
| `'dsqb'` | u32 size | dynamic sequence block |
| `'bsft'` | u32 offset | stream file table |
| `'sect'` | 4×u8 (_, category, maxActive, maxInactive) | SE category voice limits |

Any other tag goes to `readCommandMore`. It returns false in JSystem, and SMG does not override it in Petari. JSX also lists `'baac'` and `'bfca'`; those are other games' extensions.

---

## Uncertain or not found

1. **AFC decode and loop.** The DSP microcode is not in either decomp. The AFC algorithm comes from vgmstream; the coefficient table is confirmed by the decomp. That hist1 = `mpLast` is inferred, and whether `loopEnd` is inclusive or exclusive is unconfirmed.
2. **PCM8 and PCM16.** PCM8 signedness and the PCM16 byte order (assumed big-endian) are not confirmed by the decomps.
3. **BSTN section.** The extra u32 in the BSTN section header comes from Petari's header only.
4. **BST typeID low nibble.** Its meaning is unknown.
5. **Bus routing.** That bus 1/2 = main L/R and bus 3/4 = FX is inferred from the mix configs. The exact DSP resampling filter is not reproduced here.
6. **Pitch parameter scale.** The pitch parameter scale of `×bendSense/3` octaves is verbatim decomp code, so it is authoritative, even if large.
7. **JAudio1 opcodes.** The JAudio1 opcode meanings from the brief (0x80/0x88 waits, 0x9x perf) do not exist in JAudio2. JaiSeqX's `seqdoc.txt` mixes the v1 and v2 dialects; do not use it for SMG opcode numbers.
