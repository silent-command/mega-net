#!/usr/bin/env python3
"""Drives spike_server on the MEGA65: two TCP echo lines on 6400, a third
caller refused while both are busy, then a UDP echo on 6401.

    python3 tools/server_test.py 192.168.1.252
"""
import socket, sys, time

host = sys.argv[1]
PORT, UPORT = 6400, 6401
ok = True

def check(cond, what):
    global ok
    print(("  ok   " if cond else "  FAIL ") + what)
    ok = ok and cond

def echo_line(s, line, name):
    s.sendall(line)
    s.settimeout(5)
    got = b""
    while len(got) < len(line):
        chunk = s.recv(4096)
        if not chunk:
            break
        got += chunk
    check(got == line, f"{name}: {len(line)} bytes echoed intact")

a = socket.create_connection((host, PORT), timeout=5)
b = socket.create_connection((host, PORT), timeout=5)
print("two callers connected")
echo_line(a, b"hello from A\r\n", "A")
echo_line(b, b"hello from B\r\n", "B")
echo_line(a, bytes(range(256)) * 3, "A")          # 768 bytes: more than one segment
echo_line(b, b"B again\r\n", "B")

# both lines busy: a third caller must be refused at once
t0 = time.time()
try:
    c = socket.create_connection((host, PORT), timeout=5)
    c.close()
    check(False, "third caller refused")
except ConnectionRefusedError:
    check(time.time() - t0 < 2, f"third caller refused in {time.time() - t0:.2f} s (RST, not a timeout)")
except socket.timeout:
    check(False, "third caller: timed out instead of RST")

# a long echo: 16 KB one way and back, in one line, so the sender keeps
# several segments in flight (5.17); the MEGA65 echoes in 512-byte reads
big = bytes((i * 131 + 7) & 0xff for i in range(16384))
t0 = time.time()
a.sendall(big[:4096]); got = b""                   # feed 4 KB at a time so the echo drains the ring
sent = 4096
a.settimeout(10)
while len(got) < len(big):
    chunk = a.recv(8192)
    if not chunk: break
    got += chunk
    if sent < len(big) and len(got) >= sent - 2048:
        a.sendall(big[sent:sent + 4096]); sent += 4096
dt = time.time() - t0
check(got == big, f"A: 16 KB echoed intact in {dt:.2f} s ({2 * len(big) / dt / 1024:.0f} KB/s both ways)")

a.close()
time.sleep(1.0)                                    # the line closes and listens again
d = socket.create_connection((host, PORT), timeout=5)
echo_line(d, b"D took A's line\r\n", "D")
d.close(); b.close()

u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
u.settimeout(5)
u.sendto(b"udp echo?", (host, UPORT))
try:
    data, addr = u.recvfrom(1024)
    check(data == b"udp echo?" and addr[0] == host, f"UDP datagram echoed from {addr[0]}:{addr[1]}")
except socket.timeout:
    check(False, "UDP echo: no reply")
print("ALL OK" if ok else "FAILURES")
sys.exit(0 if ok else 1)
