# The console's log: Neutrino's udptty broadcasts the EE's printf on UDP 18194. usage: python tools/udptty.py [out.log]
import socket, sys

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("", 18194))
out = open(sys.argv[1] if len(sys.argv) > 1 else "udptty.log", "ab")
while True:
    d = s.recv(2048)
    sys.stdout.buffer.write(d); sys.stdout.flush()
    out.write(d); out.flush()
