#!/usr/bin/env bash
# Verify ETag / Last-Modified / 304 conditional GET handling.
set -eu
PORT=${PORT:-18307}
ROOT=$(mktemp -d)
trap 'pkill -9 -f "tinyserve -p $PORT" 2>/dev/null || true; rm -rf "$ROOT"' EXIT

echo hello > "$ROOT/i.html"
# Pin mtime so ETag and Last-Modified are deterministic for the test.
touch -d '2025-01-02 03:04:05 UTC' "$ROOT/i.html"

./build/tinyserve -p "$PORT" -d "$ROOT" </dev/null >/tmp/ts-304.log 2>&1 & disown
sleep 0.3

base=http://127.0.0.1:$PORT/i.html
etag=$(curl -sI "$base" | sed -n 's/^[Ee][Tt][Aa][Gg]: //p' | tr -d '\r')
lm=$(curl -sI "$base" | sed -n 's/^[Ll]ast-[Mm]odified: //p' | tr -d '\r')

echo "etag=$etag"
echo "lm=$lm"

[ -n "$etag" ] || { echo "FAIL: missing ETag"; exit 1; }
[ -n "$lm" ]   || { echo "FAIL: missing Last-Modified"; exit 1; }

s_etag=$(curl -s -o /dev/null -w '%{http_code}' -H "If-None-Match: $etag" "$base")
s_ims=$(curl  -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: $lm" "$base")
s_old=$(curl  -s -o /dev/null -w '%{http_code}' -H "If-Modified-Since: Wed, 01 Jan 2020 00:00:00 GMT" "$base")
s_star=$(curl -s -o /dev/null -w '%{http_code}' -H 'If-None-Match: *' "$base")

echo "etag_match=$s_etag ims_match=$s_ims ims_old=$s_old wildcard=$s_star"

if [ "$s_etag" = "304" ] && [ "$s_ims" = "304" ] && \
   [ "$s_old"  = "200" ] && [ "$s_star" = "304" ]; then
    echo PASS
else
    echo FAIL
    exit 1
fi
