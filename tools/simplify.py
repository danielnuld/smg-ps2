"""meshoptimizer's simplifier (vendor/meshoptimizer v1.3, MIT) through ctypes.

Build once (MinGW g++ on Windows):
  g++ -O2 -shared -static -static-libgcc -static-libstdc++ -o bin/meshopt.dll \
      vendor/meshoptimizer/src/simplifier.cpp vendor/meshoptimizer/src/allocator.cpp
API: meshoptimizer.h:517 meshopt_simplify(dst, indices, index_count, positions, vertex_count, stride,
     target_index_count, target_error, options, result_error) -> new index count.
"""
import ctypes
import sys
from pathlib import Path

DLL = Path(__file__).resolve().parent.parent / "bin/meshopt.dll"
_lib = None


def _api():
    global _lib
    if _lib is None:
        _lib = ctypes.CDLL(str(DLL))
        f = _lib.meshopt_simplify
        f.restype = ctypes.c_size_t
        f.argtypes = [ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.c_size_t,
                      ctypes.POINTER(ctypes.c_float), ctypes.c_size_t, ctypes.c_size_t, ctypes.c_size_t,
                      ctypes.c_float, ctypes.c_uint, ctypes.POINTER(ctypes.c_float)]
    return _lib.meshopt_simplify


def simplify(indices, positions, ratio, target_error=1.0):
    """indices: flat triangle list; positions: flat xyz floats. -> (new indices, achieved relative error).
    target_error 1.0 = no error cap, so the triangle target decides (ratio is what the sweep varies)."""
    n = len(indices)
    target = max(3, int(n * ratio) // 3 * 3)
    if ratio >= 1 or n <= 3:
        return list(indices), 0.0
    idx = (ctypes.c_uint * n)(*indices)
    pos = (ctypes.c_float * len(positions))(*positions)
    dst = (ctypes.c_uint * n)()
    err = ctypes.c_float()
    m = _api()(dst, idx, n, pos, len(positions) // 3, 12, target, target_error, 0, ctypes.byref(err))
    return list(dst[:m]), err.value


def selftest():
    # flat 21x21 grid, 800 triangles: a plane simplifies far below 25 % with ~0 error
    g = 21
    pos = [c for y in range(g) for x in range(g) for c in (float(x), float(y), 0.0)]
    idx = []
    for y in range(g - 1):
        for x in range(g - 1):
            a = y * g + x
            idx += [a, a + 1, a + g, a + 1, a + g + 1, a + g]
    out, err = simplify(idx, pos, 0.25)
    assert len(idx) == 2400 and 0 < len(out) <= 600 and err < 1e-3, (len(out), err)
    assert simplify(idx, pos, 1.0)[0] == idx
    print("selftest ok", len(idx) // 3, "->", len(out) // 3, "tris, err", err)


if __name__ == "__main__":
    selftest()
