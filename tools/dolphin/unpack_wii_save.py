"""Unpack a Wii SD-card save (data.bin) into plain files. Layout and keys from Dolphin:
WiiSaveStructs.h (Header 0xF0C0 encrypted, BkHeader 0x80, FileHDR 0x80 + data rounded to 0x40),
WiiSave.cpp:47-48 (SD IV), IOS/IOSC.cpp:609-613 (SD key)."""
import struct, sys
from pathlib import Path
from Crypto.Cipher import AES
KEY = bytes.fromhex("ab01b9d8e1622b08afbad84dbfc2a55d")
IV = bytes.fromhex("216712e6aa1f689f95c5a22324dc6a98")
src, out = Path(sys.argv[1]), Path(sys.argv[2])
d = src.read_bytes()
hdr = AES.new(KEY, AES.MODE_CBC, IV).decrypt(d[:0xF0C0])
tid, banner_size = struct.unpack(">QI", hdr[:12])
out.mkdir(parents=True, exist_ok=True)
(out / "banner.bin").write_bytes(hdr[0x20:0x20 + banner_size])
bk = d[0xF0C0:0xF140]
size, magic, ngid, nfiles, files_size = struct.unpack(">IIIII", bk[:20])
assert magic == 0x426B0001, hex(magic)
o = 0xF140
for _ in range(nfiles):
    fmagic, fsize, perm, attr, ftype = struct.unpack(">IIBBB", d[o:o + 11])
    assert fmagic == 0x03ADF17E, hex(fmagic)
    name = d[o + 11:o + 11 + 0x40].split(b"\0")[0].decode()
    fiv = d[o + 0x50:o + 0x60]
    o += 0x80
    if ftype == 2:
        (out / name).mkdir(parents=True, exist_ok=True)
        continue
    rounded = (fsize + 0x3F) & ~0x3F
    data = AES.new(KEY, AES.MODE_CBC, fiv).decrypt(d[o:o + rounded])[:fsize]
    (out / name).parent.mkdir(parents=True, exist_ok=True)
    (out / name).write_bytes(data)
    print(f"{name} {fsize} B perm {perm:02x} attr {attr:02x}")
    o += rounded
print(f"title {tid:016x}, banner {banner_size} B, {nfiles} files")
