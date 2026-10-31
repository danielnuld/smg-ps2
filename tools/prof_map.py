"""The sampling profiler's dump (engine/src/prof.c, XF=-DSAMPLE_PROF bench builds) by function.

usage: python3 tools/prof_map.py <emulog> <nm -n of the ELF> [top n]
prints the samples by function ("prof" lines: self time, 1 sample ~0.1 ms) and the library calls' callers
("callp": strcmp / strncmp / strstr / memset / cosf / sinf calls, one in 16 counted) by function."""
import sys, re, bisect, collections
syms = []
for l in open(sys.argv[2], errors='replace'):
    p = l.split()
    if len(p) == 3 and p[1] in 'tTwW':
        syms.append((int(p[0], 16) & 0xFFFFFF, p[2]))
syms.sort()
addrs = [a for a, _ in syms]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 60
def fn(a):
    i = bisect.bisect_right(addrs, a) - 1
    return syms[i][1] if i >= 0 else '?'
for tag in ('prof', 'callp'):
    tot = collections.Counter()
    n = 0
    for l in open(sys.argv[1], errors='replace'):
        if not re.search(r'\] %s [0-9a-f]' % tag, l):
            continue
        for a, c in re.findall(r' ([0-9a-f]{6}):(\d+)', l):
            a = int(a, 16); c = int(c)
            tot[fn(a)] += c
            n += c
    print('==', tag, n)
    for f, c in tot.most_common(N):
        print('%6d %s' % (c, f))
for l in open(sys.argv[1], errors='replace'):
    if 'prof:' in l: print(l.strip())
