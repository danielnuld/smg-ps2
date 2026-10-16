"""Write / dump Dolphin .dtm movies for a Wii game (Wiimote 1 + Nunchuk), matching
dolphin-emu master (Movie.h DTMHeader, DesiredWiimoteState.cpp SerializeDesiredState).

Movie input = one entry per emulated-Wiimote poll (BTEmu::Update, ~187.5 Hz: IPC_HLE at
1500 Hz, poll when > 729e6/200 ticks elapsed -> every 8th IPC tick), NOT per video frame.
Each entry: u8 length + SerializedWiimoteState bytes. No report-mode or encryption concerns:
Dolphin rebuilds the actual HID report from this "desired state".

  python dtm.py write timeline.json out.dtm   # timeline -> movie
  python dtm.py dump some.dtm                 # header + first entries (calibrate vs a GUI recording)
  python dtm.py                               # self-check
"""
import json
import struct
import sys

POLL_HZ = 1500 / 8          # emulated Wiimote polls per second (deterministic)
FPS = 60000 / 1001          # NTSC VI rate (59.94)

# Wiimote accel, 10-bit (calibration 8-bit << 2): zero 0x80, 1g = 0x9A
WM_ZERO, WM_1G = 0x80 << 2, (0x9A - 0x80) << 2          # 512, 104 counts/g
WM_REST = (WM_ZERO, WM_ZERO, WM_ZERO + WM_1G)            # == DesiredWiimoteState::DEFAULT_ACCELERATION
# Nunchuk accel: zero 0x80, 1g = 0xB3
NC_ZERO, NC_1G = 0x80 << 2, (0xB3 - 0x80) << 2           # 512, 204 counts/g
NC_REST = (NC_ZERO, NC_ZERO, NC_ZERO + NC_1G)
NC_STICK_CENTER, NC_STICK_GATE = 0x80, 0x60              # Dolphin's emulated gate radius

# SerializeDesiredState button bytes
BTN = {'A': 0x01, 'B': 0x02, '+': 0x04, '-': 0x08, '1': 0x10, '2': 0x20, 'HOME': 0x40}
DPAD = {'UP': 0x01, 'DOWN': 0x02, 'LEFT': 0x04, 'RIGHT': 0x08}
EXT_NUNCHUK = 1             # index in DesiredExtensionState variant (ExtensionNumber::NUNCHUK)

# DTMHeader, 256 bytes, packed little-endian
HEADER = [
    ('filetype', '4s'), ('gameID', '6s'), ('bWii', 'B'), ('controllers', 'B'),
    ('bFromSaveState', 'B'), ('frameCount', 'Q'), ('inputCount', 'Q'), ('lagCount', 'Q'),
    ('uniqueID', 'Q'), ('numRerecords', 'I'), ('author', '32s'), ('videoBackend', '16s'),
    ('audioEmulator', '16s'), ('md5', '16s'), ('recordingStartTime', 'Q'),
    ('bSaveConfig', 'B'), ('bSkipIdle', 'B'), ('bDualCore', 'B'), ('bProgressive', 'B'),
    ('bDSPHLE', 'B'), ('bFastDiscSpeed', 'B'), ('CPUCore', 'B'), ('bEFBAccessEnable', 'B'),
    ('bEFBCopyEnable', 'B'), ('bSkipEFBCopyToRam', 'B'), ('bEFBCopyCacheEnable', 'B'),
    ('bEFBEmulateFormatChanges', 'B'), ('bImmediateXFB', 'B'), ('bSkipXFBCopyToRam', 'B'),
    ('memcards', 'B'), ('bClearSave', 'B'), ('bongos', 'B'), ('bSyncGPU', 'B'),
    ('bNetPlay', 'B'), ('bPAL60', 'B'), ('language', 'B'), ('reserved3', 'B'),
    ('bFollowBranch', 'B'), ('bUseFMA', 'B'), ('GBAControllers', 'B'), ('bWidescreen', 'B'),
    ('countryCode', 'B'), ('reserved', '5s'), ('discChange', '40s'), ('revision', '20s'),
    ('DSPiromHash', 'I'), ('DSPcoefHash', 'I'), ('tickCount', 'Q'), ('reserved2', '11s'),
]
HFMT = '<' + ''.join(f for _, f in HEADER)
assert struct.calcsize(HFMT) == 256

DEFAULT_HEADER = dict(
    filetype=b'DTM\x1a', gameID=b'RMGE01', bWii=1,
    controllers=0x10,            # bit0-3 GC pads (none), bit4 = Wiimote 1
    bFromSaveState=0,            # play from boot
    numRerecords=0, author=b'script',
    videoBackend=b'D3D',         # the movie layer overrides -v / GFXBackend: pick it here
    md5=b'',                     # all zero -> CheckMD5 skipped
    recordingStartTime=1735689600,  # RTC base (2025-01-01), fixed for determinism
    bSaveConfig=1,               # apply the settings below as the "Movie" config layer
    bSkipIdle=1, bDualCore=0, bProgressive=0, bDSPHLE=1, bFastDiscSpeed=0,
    CPUCore=1,                   # 0 Interpreter, 1 JIT64, 5 CachedInterpreter
    bEFBAccessEnable=1, bEFBCopyEnable=1, bSkipEFBCopyToRam=1, bEFBCopyCacheEnable=0,
    bEFBEmulateFormatChanges=0, bImmediateXFB=0, bSkipXFBCopyToRam=1,
    memcards=0, bClearSave=0,    # 0: the User folder's save (a 121-star file) copied to the session NAND
    bongos=0, bSyncGPU=0, bNetPlay=0, bPAL60=0,
    language=1,                  # SYSCONF English
    bFollowBranch=1, bUseFMA=1, GBAControllers=0, bWidescreen=1,
    countryCode=49,              # SYSCONF country USA
    tickCount=(1 << 63) - 1,     # playback ends when ticks > tickCount: must not be 0
)


def header_bytes(n_entries, **over):
    h = dict(DEFAULT_HEADER, **over)
    h.setdefault('frameCount', int(n_entries * FPS / POLL_HZ))   # display only
    h.setdefault('inputCount', n_entries)                        # display only
    vals = [h.get(k, 0 if f[-1] in 'BIQ' else b'') for k, f in HEADER]
    return struct.pack(HFMT, *vals)


def encode(buttons=(), dpad=(), accel=WM_REST, ir=None, nunchuk=None):
    """One SerializedWiimoteState (without the length byte).
    ir: None (no IR dots) or list of up to 4 (x, y, size) camera points, x<1024, y<768.
    nunchuk: None (no extension) or dict jx, jy, accel (3x10bit), c, z (pressed bools)."""
    b = 0
    for k in buttons:
        b |= BTN[k]
    d = 0
    for k in dpad:
        d |= DPAD[k]
    has_btn = 1 if (b or d) else 0
    has_acc = 1 if tuple(accel) != WM_REST else 0
    has_ir = 1 if ir else 0
    ext = EXT_NUNCHUK if nunchuk is not None else 0
    out = bytearray([has_btn | has_acc << 1 | has_ir << 2 | ext << 4])   # bit3 = MotionPlus (off)
    if has_btn:
        out += bytes([b, d])
    if has_acc:
        x, y, z = accel
        low = (x & 3) | ((y >> 1) & 1) << 2 | ((z >> 1) & 1) << 3
        if has_btn:
            out[-1] |= low << 4
        else:
            out.append(low << 4)
        out += bytes([x >> 2, y >> 2, z >> 2])
    if has_ir:
        pts = list(ir) + [None] * (4 - len(ir))
        for p in pts:
            if p is None:
                out += b'\xff\xff\xff'      # not visible (x=0x3ff >= 1024)
            else:
                x, y, s = p
                out += bytes([x & 0xff, y & 0xff, (s & 0xf) | (x >> 8 & 3) << 4 | (y >> 8 & 3) << 6])
    if ext:
        n = nunchuk
        ax, ay, az = n.get('accel', NC_REST)
        bt = (0 if n.get('z') else 1) | (0 if n.get('c') else 2)       # 0 == pressed
        bt |= (ax & 3) << 2 | (ay & 3) << 4 | (az & 3) << 6
        out += bytes([n.get('jx', NC_STICK_CENTER), n.get('jy', NC_STICK_CENTER),
                      ax >> 2, ay >> 2, az >> 2, bt])
    assert len(out) <= 30
    return bytes(out)


def decode(e):
    """Inverse of encode (for dump / checks)."""
    f = e[0]
    r = {'buttons': [], 'dpad': [], 'accel': WM_REST, 'ir': None, 'nunchuk': None}
    p = 1
    if f & 1:
        r['buttons'] = [k for k, v in BTN.items() if e[1] & v]
        r['dpad'] = [k for k, v in DPAD.items() if e[2] & v]
        p = 3
    if f & 2:
        if f & 1:
            p -= 1
        low = e[p] >> 4
        r['accel'] = ((e[p + 1] << 2) | (low & 3), (e[p + 2] << 2) | (low >> 2 & 1) << 1,
                      (e[p + 3] << 2) | (low >> 3 & 1) << 1)   # Dolphin also expands bit0 of y/z
        p += 4
    if f & 4:
        r['ir'] = [((e[p + 3 * i + 2] >> 4 & 3) << 8 | e[p + 3 * i],
                    (e[p + 3 * i + 2] >> 6) << 8 | e[p + 3 * i + 1], e[p + 3 * i + 2] & 0xf)
                   for i in range(4)]
        p += 12
    if f & 8:
        p += 6
    if f >> 4 == EXT_NUNCHUK:
        jx, jy, ax, ay, az, bt = e[p:p + 6]
        r['nunchuk'] = {'jx': jx, 'jy': jy, 'z': not bt & 1, 'c': not bt & 2,
                        'accel': (ax << 2 | bt >> 2 & 3, ay << 2 | bt >> 4 & 3, az << 2 | bt >> 6 & 3)}
    return r


def stick(x, y):
    """x, y in -1..1 (y up) -> raw nunchuk bytes, inside Dolphin's octagon gate."""
    return (round(NC_STICK_CENTER + max(-1, min(1, x)) * NC_STICK_GATE),
            round(NC_STICK_CENTER + max(-1, min(1, y)) * NC_STICK_GATE))


# ponytail: centered pointer = two sensor-bar dots guessed from Dolphin's camera model
# (0.2 m bar, 42 deg FOV, ~2 m away). Calibrate by recording a GUI movie with the cursor
# centered and `dump`ing its IR bytes.
POINTER_CENTER = [(445, 384, 2), (579, 384, 2)]


def pointer(px, py):
    """px, py in -1..1 (screen right/down positive). Dots move opposite to the pointer."""
    dx, dy = -px * 400, -py * 300
    return [(int(x + dx), int(y + dy), s) for x, y, s in POINTER_CENTER]


SHAKE_G = 3.0       # ponytail: square-wave shake amplitude; check SMG spin threshold (Petari)
SHAKE_HALF = 4      # polls per half period (~23 Hz)


def build(timeline, poll_offset=0):
    """timeline: list of {"frame": f, ...state}; state persists until the next key sets it.
    State keys: stick [x,y], buttons [..], dpad [..], c, z, shake (bool), point [px,py] or null.
    poll_offset: polls consumed before timeline frame 0 (calibrate once from Dolphin's OSD
    Frame/Input counters; the first polls happen only once the game's BT stack is up)."""
    tl = sorted(timeline, key=lambda k: k['frame'])
    end = tl[-1]['frame']
    n = poll_offset + int(end / FPS * POLL_HZ) + 1
    st = {'stick': [0, 0], 'buttons': [], 'dpad': [], 'c': False, 'z': False,
          'shake': False, 'point': None}
    out, i, shake_t = bytearray(), 0, 0
    for p in range(n):
        frame = (p - poll_offset) * FPS / POLL_HZ
        while i < len(tl) and tl[i]['frame'] <= frame:
            st.update({k: v for k, v in tl[i].items() if k != 'frame'})
            i += 1
        acc = WM_REST
        if st['shake']:
            sgn = 1 if (shake_t // SHAKE_HALF) % 2 == 0 else -1
            acc = (min(1023, max(0, WM_ZERO + int(sgn * SHAKE_G * WM_1G))), WM_REST[1], WM_REST[2])
            shake_t += 1
        else:
            shake_t = 0
        jx, jy = stick(*st['stick'])
        e = encode(st['buttons'], st['dpad'], acc,
                   pointer(*st['point']) if st['point'] is not None else None,
                   {'jx': jx, 'jy': jy, 'c': st['c'], 'z': st['z']})
        out += bytes([len(e)]) + e
    return header_bytes(n) + bytes(out)


def entries(data):
    p, r = 256, []
    while p < len(data):
        n = data[p]
        r.append(data[p + 1:p + 1 + n])
        p += 1 + n
    return r


def dump(path, limit=40):
    data = open(path, 'rb').read()
    h = dict(zip([k for k, _ in HEADER], struct.unpack(HFMT, data[:256])))
    for k, v in h.items():
        print(f'{k:26} {v!r}')
    es = entries(data)
    print(f'{len(es)} entries')
    for j, e in enumerate(es[:limit]):
        print(j, e.hex(' '), decode(e))


def _selfcheck():
    assert encode() == b'\x00'                       # empty state = 1 byte
    e = encode(['A'], ['UP'], (513, 514, 618), [(445, 384, 2)],
               {'jx': 0xe0, 'jy': 0x80, 'c': True, 'z': False, 'accel': NC_REST})
    assert len(e) == 1 + 5 + 12 + 6                  # matches DeserializeDesiredState expected_size
    d = decode(e)
    assert d['buttons'] == ['A'] and d['dpad'] == ['UP'] and d['accel'] == (513, 514, 618)
    assert d['ir'][0] == (445, 384, 2) and d['nunchuk']['c'] and not d['nunchuk']['z']
    assert encode(nunchuk={}) == bytes([0x10, 0x80, 0x80, 0x80, 0x80, 0xb3, 0x03])
    m = build([{'frame': 0}, {'frame': 60, 'buttons': ['A']}, {'frame': 61, 'buttons': []},
               {'frame': 120, 'shake': True}, {'frame': 126, 'shake': False}])
    assert len(m) > 256 and m[:4] == b'DTM\x1a' and m[4:10] == b'RMGE01'
    assert struct.unpack_from('<Q', m, 237)[0] == (1 << 63) - 1      # tickCount offset
    es = entries(m)
    assert sum(1 for e in es if e[0] & 1) in (3, 4)   # A held ~1 frame = ~3 polls
    print('ok', len(es), 'entries')


if __name__ == '__main__':
    if len(sys.argv) == 4 and sys.argv[1] == 'write':
        open(sys.argv[3], 'wb').write(build(json.load(open(sys.argv[2]))))
    elif len(sys.argv) == 3 and sys.argv[1] == 'dump':
        dump(sys.argv[2])
    else:
        _selfcheck()
