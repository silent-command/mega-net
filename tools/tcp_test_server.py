#!/usr/bin/env python3
"""A TCP peer for the hardware test: accepts a connection, reads one
request line, sends 5,000 known bytes, closes its side, and logs what it
saw. Cross-platform, needs no root.

    python3 tools/tcp_test_server.py log.txt      (listens on :7070)

The payload is (i*31+7) & 0xff for i in 0..4999; sum16 is 47580. Larger
than the stack's 4 KB receive ring, on purpose: the window must reopen
for the transfer to complete (REQUIREMENTS.md 5.8)."""
import socket, sys, time

PORT = 7070
payload = bytes((i * 31 + 7) & 0xff for i in range(5000))
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("0.0.0.0", PORT))
srv.listen(1)
log = open(sys.argv[1], "w") if len(sys.argv) > 1 else sys.stdout
while True:
    c, a = srv.accept()
    c.settimeout(10)
    req = b""
    try:
        while not req.endswith(b"\n"):
            chunk = c.recv(64)
            if not chunk:
                break
            req += chunk
    except Exception:
        req += b"<timeout>"
    t = time.time()
    c.sendall(payload)
    c.shutdown(socket.SHUT_WR)
    try:
        while c.recv(1024):
            pass
    except Exception:
        pass
    c.close()
    log.write(f"from {a[0]}:{a[1]} request {req!r} sent {len(payload)} "
              f"sum16 {sum(payload) & 0xffff} close-done in {time.time() - t:.2f}s\n")
    log.flush()
