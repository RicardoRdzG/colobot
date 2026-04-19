#!/usr/bin/env bash
# test-headless.sh — OQ-5: verify agent server screenshot works under Xvfb
# Usage: bash .devcontainer/test-headless.sh <path-to-colobot-binary> <datadir>
set -euo pipefail

COLOBOT=${1:-./build/colobot}
DATADIR=${2:-./data}
PORT=7777
DISPLAY_NUM=99
OUTFILE=/tmp/headless-shot.png

echo "[1/5] Starting Xvfb on :$DISPLAY_NUM"
Xvfb :$DISPLAY_NUM -screen 0 1024x768x24 &
XVFB_PID=$!
export DISPLAY=:$DISPLAY_NUM
sleep 1

echo "[2/5] Starting Colobot agent server (headless)"
LIBGL_ALWAYS_SOFTWARE=1 "$COLOBOT" -agentserver $PORT -headless -datadir "$DATADIR" &
GAME_PID=$!

cleanup() {
    kill $GAME_PID 2>/dev/null || true
    kill $XVFB_PID 2>/dev/null || true
}
trap cleanup EXIT

echo "[3/5] Waiting for /health..."
for i in $(seq 1 20); do
    if curl -sf http://127.0.0.1:$PORT/health >/dev/null 2>&1; then
        echo "  -> ready after ${i}s"
        break
    fi
    sleep 1
done
curl -sf http://127.0.0.1:$PORT/health | python3 -m json.tool

echo "[4/5] Requesting screenshot..."
curl -sf http://127.0.0.1:$PORT/screenshot | python3 -c "
import sys, json, base64
d = json.load(sys.stdin)
if not d.get('ok'):
    print('ERROR:', d.get('error'))
    sys.exit(1)
png = base64.b64decode(d['data']['png'])
open('$OUTFILE', 'wb').write(png)
print(f'PNG written: {len(png)} bytes')
"

echo "[5/5] Done. Screenshot saved to $OUTFILE"
echo "      Run: xdg-open $OUTFILE  (or copy it out of the container)"
