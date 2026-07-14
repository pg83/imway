#!/usr/bin/env bash
# M2 test: typing into foot via the control channel creates a file.
# Chain: FIFO → seat → wl_keyboard → foot → pty → shell → touch.
set -euo pipefail

IMWAY="$1"

command -v foot >/dev/null || { echo "SKIP: foot not found"; exit 127; }

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
CTL="$RT/ctl"
MARKER="$RT/m2ok"

"$IMWAY" --device headless --socket imway-input --control "$CTL" --frames 1200 \
    --screenshot "$RT/shot.ppm" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -p "$CTL" && -S "$RT/imway-input" ]] && break
    sleep 0.1
done

WAYLAND_DISPLAY=imway-input foot >"$RT/foot.log" 2>&1 &
FOOT_PID=$!

# wait for map
for _ in $(seq 1 100); do
    grep -q "mapped" "$RT/imway.log" && break
    sleep 0.1
done
grep -q "mapped" "$RT/imway.log" || { echo "foot did not map"; cat "$RT/foot.log"; exit 1; }
sleep 1 # let the shell inside foot finish starting up

{
    echo "type touch $MARKER"
    sleep 0.3
    echo "key 28 press"   # Enter
    echo "key 28 release"
} >"$CTL"

for _ in $(seq 1 100); do
    [[ -e "$MARKER" ]] && break
    sleep 0.1
done

echo "quit" >"$CTL" || true
wait "$IMWAY_PID" || true
kill "$FOOT_PID" 2>/dev/null || true

[[ -e "$MARKER" ]] || {
    echo "FAIL: typed command did not run (no $MARKER)"
    tail -5 "$RT/imway.log" "$RT/foot.log"
    exit 1
}
echo "OK: keyboard input reached the shell inside foot"
