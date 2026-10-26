"""PS2 disc image of the streaming packs (task 5.2: zones read from the disc image).

usage: python tools/disc.py [packs dir, default out/packs] [disc dir, default out/disc] [iso, default out/smg-ps2.iso]

The disc dir must already hold SLICE.ELF (engine/build.sh MODE blob Galaxy/n disc); it is renamed BOOT, a serial-form
name, since launchers (ORBIT's catalog, OPL) take the boot file's name as the game's serial. Written there:
  PACKS.BIN  every file under the packs dir, each starting on a 2048-byte sector
  PACKS.TOC  u32 n, n x {char path[88] (relative, '/' separated, NUL padded), u32 offset, u32 size} (little-endian)
  SYSTEM.CNF boots cdrom0:\\SMGP_000.01;1
then the ISO through mkisofs in WSL (ISO 9660 level 1: the PS2 ROM's cdrom0: reads 8.3 names only, hence one big
file and a table instead of the packs' own names).
"""
import struct
import subprocess
import sys
from pathlib import Path

SECTOR = 2048
BOOT = "SMGP_000.01"  # not a Sony prefix: no real game's covers or saves
PATH_LEN = 88


def build(packs, disc):
    files = sorted(p for p in packs.rglob("*") if p.is_file() and p.suffix in (".bin", ".smv", ".pcm"))  # movies: tools/movies.py; music: tools/music.py (out/music copied to packs/music)
    toc, off = [], 0
    with open(disc / "PACKS.BIN", "wb") as out:
        for p in files:
            rel = p.relative_to(packs).as_posix()
            assert len(rel) < PATH_LEN, rel
            data = p.read_bytes()
            out.write(data + b"\0" * (-len(data) % SECTOR))
            toc.append(struct.pack(f"<{PATH_LEN}sII", rel.encode(), off, len(data)))
            off += len(data) + (-len(data) % SECTOR)
    (disc / "PACKS.TOC").write_bytes(struct.pack("<I", len(toc)) + b"".join(toc))
    (disc / "SYSTEM.CNF").write_bytes(f"BOOT2 = cdrom0:\\{BOOT};1\r\nVER = 1.00\r\nVMODE = NTSC\r\n".encode())
    return len(files), off


def wsl_path(p):
    p = p.resolve()
    return f"/mnt/{p.drive[0].lower()}{p.as_posix()[2:]}"


def main():
    packs = Path(sys.argv[1] if len(sys.argv) > 1 else "out/packs")
    disc = Path(sys.argv[2] if len(sys.argv) > 2 else "out/disc")
    iso = Path(sys.argv[3] if len(sys.argv) > 3 else "out/smg-ps2.iso")
    if (disc / "SLICE.ELF").exists():  # a fresh build replaces the last one
        (disc / "SLICE.ELF").replace(disc / BOOT)
    if not (disc / BOOT).exists():
        sys.exit(f"{disc / 'SLICE.ELF'} missing: engine/build.sh MODE blob Galaxy/n disc")
    n, size = build(packs, disc)
    print(f"PACKS.BIN: {n} packs, {size / 2**20:.0f} MB")
    # the boot files first (SYSTEM.CNF, the ELF, then PACKS.TOC) at the start of the disc
    order = disc / "order.txt"
    order.write_bytes("".join(f"{wsl_path(disc / f)} {w}\n" for f, w in
                              (("SYSTEM.CNF", 300), (BOOT, 200), ("PACKS.TOC", 100))).encode())
    subprocess.run(["wsl.exe", "-e", "mkisofs", "-quiet", "-iso-level", "1", "-V", "SMGPS2", "-sysid", "PLAYSTATION",
                    "-sort", wsl_path(order), "-m", "order.txt", "-o", wsl_path(iso), wsl_path(disc)], check=True)
    print(f"{iso}: {iso.stat().st_size / 2**20:.0f} MB")


def selftest():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        (d / "p" / "G" / "1").mkdir(parents=True)
        (d / "p" / "resident.bin").write_bytes(b"r" * 3000)
        (d / "p" / "G" / "1" / "scene.bin").write_bytes(b"s" * 5)
        (d / "o").mkdir()
        n, size = build(d / "p", d / "o")
        toc = (d / "o" / "PACKS.TOC").read_bytes()
        assert n == 2 and size == 3 * SECTOR, (n, size)
        ents = [struct.unpack_from(f"<{PATH_LEN}sII", toc, 4 + i * (PATH_LEN + 8)) for i in range(n)]
        assert [(e[0].rstrip(b"\0"), e[1], e[2]) for e in ents] == [(b"G/1/scene.bin", 0, 5), (b"resident.bin", 2048, 3000)]
        assert (d / "o" / "PACKS.BIN").read_bytes()[2048:2051] == b"rrr"
        assert (d / "o" / "SYSTEM.CNF").read_bytes().startswith(b"BOOT2 = cdrom0:\\SMGP_000.01;1\r\n")
    print("selftest ok")


if __name__ == "__main__":
    selftest() if sys.argv[1:] == ["--selftest"] else main()
