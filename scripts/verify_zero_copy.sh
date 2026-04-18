#!/usr/bin/env bash
# Verify zero-copy file serve: large file MD5 integrity + range integrity.
set -eu
PORT=${PORT:-18500}
ROOT=$(mktemp -d)
trap 'pkill -9 -f "tinyserve -p $PORT" 2>/dev/null || true; rm -rf "$ROOT"' EXIT

dd if=/dev/urandom of="$ROOT/big.bin" bs=1M count=50 status=none
SRC=$(md5sum "$ROOT/big.bin" | cut -d' ' -f1)

./build/tinyserve -p "$PORT" -d "$ROOT" </dev/null >/tmp/ts-zc.log 2>&1 & disown
sleep 0.3

curl -s "http://127.0.0.1:$PORT/big.bin" -o "$ROOT/dl.bin"
DL=$(md5sum "$ROOT/dl.bin" | cut -d' ' -f1)
[ "$SRC" = "$DL" ] || { echo "FULL md5 mismatch src=$SRC dl=$DL"; exit 1; }

curl -s -r 1048576-2097151 "http://127.0.0.1:$PORT/big.bin" -o "$ROOT/r.bin"
dd if="$ROOT/big.bin" bs=1 skip=1048576 count=1048576 of="$ROOT/ref.bin" status=none
cmp "$ROOT/r.bin" "$ROOT/ref.bin" || { echo "range mismatch"; exit 1; }

echo "full md5: $DL"
echo "PASS"
