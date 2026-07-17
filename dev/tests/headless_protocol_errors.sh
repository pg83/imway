#!/usr/bin/env bash
# Malformed clients get a protocol error and die; the compositor survives
# every one of them.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in self-subsurface invalid-transform defunct-subsurface duplicate-xdg invalid-configure \
            invalid-resize-edge negative-min-size conflicting-size \
            incomplete-positioner unmapped-popup-parent destroy-wm-base \
            invalid-dnd-mask duplicate-dnd-actions; do
    "$IMWAY_CLIENT" "$mode"
    kill -0 "$IMWAY_PID" || { echo "compositor died on $mode"; exit 1; }
done

echo "OK: malformed clients were disconnected and compositor survived"
