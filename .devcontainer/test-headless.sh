#!/usr/bin/env bash
# test-headless.sh — local equivalent of the agent-server-tests GHA workflow
# Runs all four milestones against a Colobot build using Xvfb + llvmpipe.
#
# Usage: bash .devcontainer/test-headless.sh <path-to-colobot-binary> <datadir>
#
# Requirements (included in devcontainer image):
#   xvfb, libgl1-mesa-dri (llvmpipe), imagemagick (import -window root)
set -euo pipefail

COLOBOT=${1:-./build/colobot}
DATADIR=${2:-./data}
PORT=7777
DISPLAY_NUM=99
OUTFILE=/tmp/agent-server-screenshot.png

echo "[setup] Starting Xvfb on :$DISPLAY_NUM"
Xvfb :$DISPLAY_NUM -screen 0 1024x768x24 &
XVFB_PID=$!
export DISPLAY=:$DISPLAY_NUM
sleep 1

echo "[setup] Starting Colobot agent server (llvmpipe software GL)"
# Do NOT use -headless: the agent server needs a real GL context for rendering.
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

echo "[setup] Waiting for /health..."
for i in $(seq 1 20); do
    if curl -sf http://127.0.0.1:$PORT/health >/dev/null 2>&1; then
        echo "  -> ready after ${i}s"
        break
    fi
    if ! kill -0 "$GAME_PID" 2>/dev/null; then
        echo "ERROR: game exited early. Log:"
        tail -30 /tmp/colobot-agent.log
        exit 1
    fi
    sleep 1
done

echo
echo "=== Milestone 0: /health ==="
curl -sf http://127.0.0.1:$PORT/health | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert d['ok'], f'health not ok: {d}'
assert d['data']['status'] == 'ok'
print('PASS')
"

echo
echo "=== Milestone 1: /state shows PlayerSelect ==="
STATE=$(curl -sf http://127.0.0.1:$PORT/state)
echo "$STATE" | python3 -c "
import sys, json
d = json.load(sys.stdin)
assert d['ok'], f'state not ok: {d}'
assert d['data']['screen'] == 'PlayerSelect', f'expected PlayerSelect, got: {d[\"data\"][\"screen\"]}'
ids = [w['id'] for w in d['data']['widgets']]
assert 'EditPlayerName' in ids, f'EditPlayerName missing from {ids}'
assert 'ButtonOK' in ids, f'ButtonOK missing from {ids}'
print('PASS')
"

echo
echo "=== Milestone 2: type player name and click OK ==="
curl -sf -X POST http://127.0.0.1:$PORT/type \
  -H 'Content-Type: application/json' \
  -d '{"id":"EditPlayerName","text":"Claude"}' >/dev/null

curl -sf -X POST http://127.0.0.1:$PORT/click \
  -H 'Content-Type: application/json' \
  -d '{"id":"ButtonOK"}' >/dev/null

sleep 2

curl -sf http://127.0.0.1:$PORT/state | python3 -c "
import sys, json
d = json.load(sys.stdin)
screen = d['data']['screen']
assert screen != 'PlayerSelect', f'still on PlayerSelect after clicking OK'
print(f'PASS (now on screen={screen})')
"

echo
echo "=== Milestone 3: /screenshot ==="
curl -sf http://127.0.0.1:$PORT/screenshot | python3 -c "
import sys, json, base64
d = json.load(sys.stdin)
assert d['ok'], f'screenshot not ok: {d}'
png = base64.b64decode(d['data']['png'])
assert len(png) > 1000, f'PNG too small: {len(png)} bytes'
assert png[:4] == b'\x89PNG', 'not a valid PNG'
open('$OUTFILE', 'wb').write(png)
print(f'PASS ({len(png)} bytes -> $OUTFILE)')
"

echo
echo "All milestones PASSED."
