"""Read/write Dolphin's emulated Wii RAM from outside (Windows, ctypes). Finds MEM1 as a mapped region whose first
bytes are the game id (the disc header sits at 0x80000000) and MEM2 as a mapped region of 64 MB right after it.

  python dolmem.py dump OUTDIR            -> OUTDIR/mem1.raw (24 MB), OUTDIR/mem2.raw (64 MB)
  python dolmem.py find TEXT              -> emulated addresses of TEXT in MEM1/MEM2
  python dolmem.py warp STAGE SCENARIO    -> keep rewriting SceneControlInfo {"Game", "AstroGalaxy"} to STAGE/SCENARIO
"""
import ctypes, ctypes.wintypes as wt, struct, subprocess, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_ALL = 0x1F0FFF
MEM_COMMIT, MEM_MAPPED = 0x1000, 0x40000
MEM1_SIZE, MEM2_SIZE = 0x1800000, 0x4000000


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", wt.DWORD),
                ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_size_t), ("State", wt.DWORD),
                ("Protect", wt.DWORD), ("Type", wt.DWORD)]


def dolphin_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Dolphin.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return int(out.split(",")[1].strip('"'))


class Ram:
    def __init__(self, game=b"RMGE01"):
        self.h = k32.OpenProcess(PROCESS_ALL, False, dolphin_pid())
        regions, addr, mbi = [], 0, MBI()
        while k32.VirtualQueryEx(self.h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
            if mbi.State == MEM_COMMIT and mbi.Type == MEM_MAPPED:
                regions.append((mbi.BaseAddress or 0, mbi.RegionSize))
            addr = (mbi.BaseAddress or 0) + mbi.RegionSize
            if addr >= 0x7FFFFFFF0000:
                break
        self.mem1 = next(b for b, s in regions if s >= MEM1_SIZE and self._read(b, 6) == game)
        self.mem2 = next(b for b, s in regions if s >= MEM2_SIZE and b != self.mem1 and self._read(b, 4) is not None
                         and b > self.mem1)

    def _read(self, host, n):
        buf, got = ctypes.create_string_buffer(n), ctypes.c_size_t()
        ok = k32.ReadProcessMemory(self.h, ctypes.c_void_p(host), buf, n, ctypes.byref(got))
        return buf.raw[:got.value] if ok else None

    def host(self, addr):
        return self.mem1 + (addr & 0x1FFFFFF) if addr & 0x10000000 == 0 else self.mem2 + (addr & 0x3FFFFFF)

    def read(self, addr, n):
        return self._read(self.host(addr), n)

    def write(self, addr, data):
        k32.WriteProcessMemory(self.h, ctypes.c_void_p(self.host(addr)), data, len(data), None)

    def blocks(self):
        return [(0x80000000, self._read(self.mem1, MEM1_SIZE)), (0x90000000, self._read(self.mem2, MEM2_SIZE))]


def find(ram, needle):
    out = []
    for base, data in ram.blocks():
        i = data.find(needle)
        while i >= 0:
            out.append(base + i)
            i = data.find(needle, i + 1)
    return out


def warp(stage, scenario, seconds=120):
    """SceneControlInfo: char mScene[32], char mStage[32], s32 mScenarioNo, s32 mSelectedScenarioNo
    (Petari GameSystemSceneController.hpp:17-31). Rewrites every {"Game", "AstroGalaxy"} found until the time is up."""
    ram = Ram()
    pat = b"Game".ljust(32, b"\0") + b"AstroGalaxy\0"
    new = stage.encode().ljust(32, b"\0") + struct.pack(">ii", scenario, scenario)
    end, hits = time.time() + seconds, set()
    while time.time() < end:
        mem1 = ram._read(ram.mem1, MEM1_SIZE)                  # SceneControlInfo lives in MEM1
        i, found = mem1.find(pat), []
        while i >= 0:
            found.append(0x80000000 + i)
            i = mem1.find(pat, i + 1)
        for a in found:
            ram.write(a + 0x20, new)
            if a not in hits:
                hits.add(a)
                print(f"rewrote SceneControlInfo at {a:08X} -> {stage} {scenario}", flush=True)
        time.sleep(0.05)
    return hits


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "dump":
        from pathlib import Path
        out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
        ram = Ram()
        (out / "mem1.raw").write_bytes(ram._read(ram.mem1, MEM1_SIZE))
        (out / "mem2.raw").write_bytes(ram._read(ram.mem2, MEM2_SIZE))
        print("dumped", out)
    elif cmd == "find":
        ram = Ram()
        print([f"{a:08X}" for a in find(ram, sys.argv[2].encode())][:20])
    elif cmd == "near":  # near X Y Z R: big-endian float triples within R of (X, Y, Z) (Mario's position...)
        import numpy as np
        a = " ".join(sys.argv[2:]).replace(",", " ").split()  # "x y z" as one argument too
        ram, c, r = Ram(), np.array([float(v) for v in a[:3]]), float(a[3])
        for base, data in ram.blocks():
            f = np.frombuffer(data[:len(data) // 4 * 4], ">f4").astype(np.float64)
            d = (f[:-2] - c[0]) ** 2 + (f[1:-1] - c[1]) ** 2 + (f[2:] - c[2]) ** 2
            for i in np.nonzero(d < r * r)[0][:400]:
                print(f"{base + 4 * i:08X} {f[i]:.1f} {f[i + 1]:.1f} {f[i + 2]:.1f}")
    elif cmd == "read3":  # read3 ADDR...: big-endian float triples at those addresses
        ram = Ram()
        for a in sys.argv[2:]:
            print(a, struct.unpack(">3f", ram.read(int(a, 16), 12)))
    elif cmd == "warp":
        warp(sys.argv[2], int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 120)
