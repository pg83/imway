#!/usr/bin/env bash
# Malformed clients get a protocol error and die; the compositor survives
# every one of them.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in self-subsurface invalid-transform defunct-subsurface duplicate-xdg invalid-configure \
            invalid-resize-edge negative-min-size conflicting-size \
            incomplete-positioner unmapped-popup-parent destroy-wm-base \
            invalid-dnd-mask duplicate-dnd-actions \
            shm-bad-format shm-bad-stride shm-pool-shrink shm-pool-zero shm-pool-badfd \
            attach-offset subsurface-place-stranger positioner-zero-anchor \
            popup-bad-positioner popup-parent-no-role xdg-double-role \
            viewport-dead-source tearing-twice fifo-dead-surface \
            commit-timer-dead-surface \
            content-type-twice alpha-mod-twice alpha-mod-dead-surface \
            dmabuf-params-incomplete representation-dead-alpha \
            representation-dead-coefficients representation-dead-chroma; do
    "$IMWAY_CLIENT" "$mode"
    expect_alive "compositor died on $mode"
done

echo "OK: malformed clients were disconnected and compositor survived"
