#!/usr/bin/env bash
set -euo pipefail

IMWAY="$1"
CLIENT="$2"
RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"

"$IMWAY" --device headless --socket imway-test --control "$RT/ctl" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" && -p "$RT/ctl" ]] && break
    sleep 0.1
done

[[ -S "$RT/imway-test" ]] || { cat "$RT/imway.log"; exit 1; }

for mode in self-subsurface invalid-transform defunct-subsurface duplicate-xdg invalid-configure \
            invalid-resize-edge negative-min-size conflicting-size \
            incomplete-positioner unmapped-popup-parent destroy-wm-base \
            invalid-dnd-mask duplicate-dnd-actions; do
    WAYLAND_DISPLAY=imway-test "$CLIENT" "$mode"
    kill -0 "$IMWAY_PID"
done

echo quit >"$RT/ctl"
wait "$IMWAY_PID"

echo "OK: malformed clients were disconnected and compositor survived"
