#!/usr/bin/env bash
# Connection cap verification: -n 3, send 20 concurrent conns, expect some 503.
set -u
pkill -9 tinyserve 2>/dev/null || true
sleep 0.3
mkdir -p /tmp/ts-www
echo ok > /tmp/ts-www/i.html

./build/tinyserve -p 18000 -n 3 -d /tmp/ts-www </dev/null >/tmp/ts.log 2>&1 &
SRV=$!
disown "$SRV" 2>/dev/null || true
sleep 0.3

TMP=$(mktemp -d)
for i in $(seq 1 20); do
  ( curl -s -o /dev/null -m 2 -w "%{http_code}\n" http://127.0.0.1:18000/i.html > "$TMP/$i" ) &
done
wait

echo "--- status code histogram ---"
cat "$TMP"/* | sort | uniq -c | sort -rn
rm -rf "$TMP"

kill -9 "$SRV" 2>/dev/null || true
