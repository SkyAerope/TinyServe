#!/usr/bin/env bash
# Verify that directory listing is async: a slow big-dir listing must
# not block concurrent small-file requests.
set -u
pkill -9 tinyserve 2>/dev/null || true
sleep 0.2
ROOT=/tmp/ts-dir
rm -rf "$ROOT"
mkdir -p "$ROOT/big"
echo small > "$ROOT/small.html"
# Make a moderately large directory (5000 entries so stat()s add latency).
for i in $(seq 1 5000); do : > "$ROOT/big/file_$i.txt"; done

./build/tinyserve -p 18004 -d "$ROOT" </dev/null >/tmp/ts.log 2>&1 &
SRV=$!
disown "$SRV" 2>/dev/null || true
sleep 0.3

# 1) Sanity: big dir listing still returns 200 and contains many entries.
LST=$(curl -s http://127.0.0.1:18004/big/ | grep -c 'file_')
echo "big-dir entries: $LST"

# 2) Launch big-dir listing and small-file request concurrently; record times.
T0=$(date +%s%N)
curl -s -o /dev/null http://127.0.0.1:18004/big/ &
BIG=$!
# Give the big listing a tiny head start then fire small request.
sleep 0.02
S0=$(date +%s%N)
curl -s -o /dev/null http://127.0.0.1:18004/small.html
S1=$(date +%s%N)
wait "$BIG"
T1=$(date +%s%N)
SMALL_MS=$(( (S1 - S0) / 1000000 ))
BIG_MS=$(( (T1 - T0) / 1000000 ))
echo "small_ms=$SMALL_MS big_ms=$BIG_MS"

kill -9 "$SRV" 2>/dev/null || true

# Expect small request to finish roughly independently — much faster than big.
# Concrete bound: small_ms < big_ms / 2 (very generous).
if [ "$LST" -ge 5000 ] && [ "$SMALL_MS" -lt $(( BIG_MS / 2 + 1 )) ]; then
  echo "PASS"
  exit 0
fi
echo "FAIL"
exit 1
