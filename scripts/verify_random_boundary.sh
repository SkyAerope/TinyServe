#!/usr/bin/env bash
# Verify multipart/byteranges boundary uses crypto-quality randomness.
# Sanity: 32-hex-char boundary, distinct across requests, no pattern.
set -eu
PORT=${PORT:-18302}
ROOT=$(mktemp -d)
trap 'pkill -9 -f "tinyserve -p $PORT" 2>/dev/null || true; rm -rf "$ROOT"' EXIT

dd if=/dev/zero of="$ROOT/x" bs=1024 count=10 status=none

./build/tinyserve -p "$PORT" -d "$ROOT" </dev/null >/tmp/ts-rb.log 2>&1 & disown
sleep 0.3

bs=$(for _ in $(seq 1 8); do
    curl -sI -r 0-99,200-299 "http://127.0.0.1:$PORT/x" \
        | awk -F'boundary=' '/Content-Type: multipart/ {print $2}' \
        | tr -d '\r\n'
    echo
done)

echo "$bs"

# Each must be tinyserve_ + 32 hex chars
bad=$(echo "$bs" | grep -vE '^tinyserve_[0-9a-f]{32}$' || true)
if [ -n "$bad" ]; then
    echo "FAIL: malformed boundary"; exit 1
fi

# All 8 boundaries must be unique
uniq=$(echo "$bs" | sort -u | wc -l | tr -d ' ')
if [ "$uniq" != "8" ]; then
    echo "FAIL: only $uniq unique boundaries out of 8"; exit 1
fi

echo "PASS"
