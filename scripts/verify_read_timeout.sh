#!/usr/bin/env bash
# Request read-timeout verification: send partial HTTP request and
# verify that the server closes (or 408's) within read_timeout_ms.
set -u
pkill -9 tinyserve 2>/dev/null || true
sleep 0.2
mkdir -p /tmp/ts-www
echo ok > /tmp/ts-www/i.html

./build/tinyserve -p 18003 -d /tmp/ts-www </dev/null >/tmp/ts.log 2>&1 &
SRV=$!
disown "$SRV" 2>/dev/null || true
sleep 0.3

python3 - <<'PY'
import socket, time, sys
s = socket.create_connection(("127.0.0.1", 18003), timeout=5)
# Send just the request line; deliberately withhold the rest.
s.sendall(b"GET /i.html HTTP/1.1\r\nHost: x\r\n")
s.settimeout(20)
t0 = time.time()
try:
    data = b""
    while True:
        c = s.recv(4096)
        if not c:
            break
        data += c
    elapsed = time.time() - t0
    has_408 = b"408" in data[:80]
    print(f"closed after {elapsed:.1f}s, got_408={has_408}")
    # Expect ~10s + jitter, and a 408 response.
    if 8 <= elapsed <= 15 and has_408:
        print("PASS")
        sys.exit(0)
    print(f"FAIL: elapsed={elapsed:.1f}, 408={has_408}")
    sys.exit(1)
except socket.timeout:
    print(f"FAIL: still open after {time.time()-t0:.1f}s")
    sys.exit(1)
PY
RC=$?
kill -9 "$SRV" 2>/dev/null || true
exit $RC
