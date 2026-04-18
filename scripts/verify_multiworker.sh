#!/usr/bin/env bash
# Verify multi-worker mode: -j N forks N children that all share the
# listening port via SO_REUSEPORT, and SIGTERM to the supervisor cleanly
# stops every worker.
set -eu
PORT=${PORT:-18520}
N=${N:-4}
ROOT=$(mktemp -d)
LOG=$(mktemp)
trap 'pkill -TERM -f "tinyserve -p $PORT" 2>/dev/null || true; sleep 0.3; pkill -9 -f "tinyserve -p $PORT" 2>/dev/null || true; rm -rf "$ROOT" "$LOG"' EXIT

echo hello > "$ROOT/i.html"
./build/tinyserve -p "$PORT" -d "$ROOT" -j "$N" </dev/null >"$LOG" 2>&1 &
sup_pid=$!
sleep 0.6

# Count tinyserve processes (supervisor + N workers).
total=$(pgrep -f "tinyserve -p $PORT" | wc -l | tr -d ' ')
echo "processes=$total (expected $((N+1)))"
[ "$total" = "$((N+1))" ] || { echo FAIL_FORK_COUNT; exit 1; }

# Hit the port from many clients; without SO_REUSEPORT this would fail.
ok=0
for i in $(seq 1 32); do
    body=$(curl -fs --max-time 2 "http://127.0.0.1:$PORT/i.html" || true)
    [ "$body" = "hello" ] && ok=$((ok+1))
done
echo "responses_ok=$ok / 32"
[ "$ok" = "32" ] || { echo FAIL_RESPONSES; exit 1; }

# Clean shutdown: SIGTERM the supervisor and every worker should exit.
kill -TERM "$sup_pid" 2>/dev/null || true
sleep 0.5
remaining=$(pgrep -f "tinyserve -p $PORT" | wc -l | tr -d ' ')
echo "remaining_after_sigterm=$remaining"
[ "$remaining" = "0" ] || { echo FAIL_SHUTDOWN; exit 1; }

echo PASS
