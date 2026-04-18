#!/usr/bin/env bash
# Verify HTTP 431 is returned when the request header section exceeds
# TS_MAX_HEADER_SIZE (8 KiB).
set -eu
PORT=${PORT:-18304}
ROOT=$(mktemp -d)
trap 'pkill -9 -f "tinyserve -p $PORT" 2>/dev/null || true; rm -rf "$ROOT"' EXIT

mkdir -p "$ROOT"
echo ok > "$ROOT/i.html"

./build/tinyserve -p "$PORT" -d "$ROOT" </dev/null >/tmp/ts-431.log 2>&1 & disown
sleep 0.3

# Send ~9 KiB of header data (above the 8 KiB cap).
status=$( (printf 'GET /i.html HTTP/1.1\r\nHost: x\r\nX-Big: ';
           head -c 9000 /dev/urandom | base64 | tr -d '\n';
           printf '\r\n\r\n') \
          | nc -w 3 127.0.0.1 "$PORT" \
          | head -1 \
          | awk '{print $2}' )

# Sanity: a small request should still get 200/304/etc., not 431.
ok_status=$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/i.html")

echo "oversize=$status normal=$ok_status"

if [ "$status" = "431" ] && [ "$ok_status" = "200" ]; then
    echo PASS
else
    echo FAIL
    exit 1
fi
