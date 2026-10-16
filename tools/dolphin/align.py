"""Cut point for a Wii | PS2 side by side: the PS2 capture time of g 0, found by correlating the frame-to-frame
motion of both videos from g 90 (the first stick move of play/moves.json) for 6 s.
usage: align.py <wii.avi> <ps2.mp4> <wii g0 seconds> <ps2 search from> <ps2 search to>"""
import subprocess, sys, numpy as np
def motion(path, ss, dur, fps=30):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(ss), '-i', path, '-t', str(dur), '-vf', f'fps={fps},scale=64:48,format=gray', '-f', 'rawvideo', '-'], capture_output=True).stdout
    f = np.frombuffer(raw, np.uint8).reshape(-1, 48, 64).astype(np.float32)
    return np.abs(np.diff(f, axis=0)).mean((1, 2))
wii, ps2, w0, pmin, pmax = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
a = motion(wii, w0 + 1.5, 6)            # Wii g 90 .. g 450
b = motion(ps2, pmin, pmax - pmin + 7)
best = None
for k in range(0, len(b) - len(a)):
    x = b[k:k + len(a)]
    c = np.corrcoef(a, x)[0, 1]
    if best is None or c > best[0]: best = (c, k)
c, k = best
g90 = pmin + k / 30
print('corr %.3f  ps2 g90 at %.2f  -> g0 at %.2f' % (c, g90, g90 - 1.5))
