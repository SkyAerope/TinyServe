#!/usr/bin/env bash
# Idle keep-alive timeout verification using Python for reliable timing.
set -u
pkill -9 tinyserve 2>/dev/null || true
sleep 0.2
mkdir -p /tmp/ts-www
echo ok > /tmp/ts-www/i.html

./build/tinyserve -p 18001 -d /tmp/ts-www </dev/null >/tmp/ts.log 2>&1 &
SRV=$!
disown "$SRV" 2>/dev/null || true
sleep 0.3

python3 - <<'PY'
import socket, time, sys
s = socket.create_connection(("127.0.0.1", 18001), timeout=5)
s.sendall(b"GET /i.html HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
hdr = b""
while b"\r\n\r\n" not in hdr:
    c = s.recv(4096)
    if not c: break
    hdr += c
s.settimeout(2)
try:
    while True:
        c = s.recv(4096)
        if not c: break
except socket.timeout:
    pass
s.settimeout(40)
t0 = time.time()
try:
    data = s.recv(1)
    elapsed = time.time() - t0
    if not data:
        print(f"server closed after {elapsed:.1f}s")
        sys.exit(0 if 25 <= elapsed <= 35 else 1)
    else:
        print("FAIL: unexpected data")
        sys.exit(1)
except socket.timeout:
    print(f"FAIL: still open after {time.time()-t0:.1f}s")
    sys.exit(1)
PY
RC=$?
kill -9 "$SRV" 2>/dev/null || true
exit $RC
