#!/usr/bin/env bash
# test-headless.sh — OQ-5: verify agent server screenshot works under Xvfb + llvmpipe
# Usage: bash .devcontainer/test-headless.sh <path-to-colobot-binary> <datadir>
#
# Requirements (included in devcontainer image):
#   xvfb, libgl1-mesa-dri (llvmpipe), imagemagick (for 'import -window root')
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

echo "[2/5] Starting Colobot agent server (software GL via llvmpipe)"
# Note: do NOT use -headless; a real window is needed for GL context + screengrab.
# LIBGL_ALWAYS_SOFTWARE=1 forces Mesa llvmpipe; no hardware GPU required.
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  "$COLOBOT" -agentserver "$PORT" \
  -datadir "$DATADIR" \
  -glversion 3.3 -glprofile core \
  -resolution 640x480 </dev/null >/tmp/colobot-agent.log 2>&1 &
GAME_PID=$!

cleanup() {
    kill "$GAME_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

echo "[3/5] Waiting for /health..."
for i in $(seq 1 20); do
    if curl -sf http://127.0.0.1:$PORT/health >/dev/null 2>&1; then
        echo "  -> ready after ${i}s"
        break
    fi
    if ! kill -0 "$GAME_PID" 2>/dev/null; then
        echo "ERROR: game exited early. Log:"
        tail -20 /tmp/colobot-agent.log
        exit 1
    fi
    sleep 1
done
curl -s http://127.0.0.1:$PORT/health | python3 -m json.tool

echo "[4/5] Requesting screenshot..."
curl -s --max-time 10 http://127.0.0.1:$PORT/screenshot | python3 -c "
import sys, json, base64
d = json.load(sys.stdin)
if not d.get('ok'):
    print('ERROR:', d.get('error'))
    sys.exit(1)
png = base64.b64decode(d['data']['png'])
open('$OUTFILE', 'wb').write(png)
print(f'PNG written: {len(png)} bytes -> $OUTFILE')
"

echo "[5/5] OQ-5 PASS"
