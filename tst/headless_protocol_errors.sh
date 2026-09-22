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
            representation-dead-coefficients representation-dead-chroma \
            negative-max-size dmabuf-params-plane-gap export-plain-surface \
            icon-not-shm icon-not-square icon-zero-scale icon-thin-stride \
            icon-sigbus reposition-bad-positioner \
            security-bad-listen-fd security-incomplete security-engine-after-commit \
            security-appid-after-commit security-instance-after-commit \
            security-commit-twice security-listen-not-socket security-no-app-id \
            security-no-instance security-engine-twice security-app-id-twice \
            security-instance-twice \
            capture-bad-option capture-bad-damage capture-damage-left \
            capture-damage-above capture-damage-flat capture-attach-after \
            capture-damage-after capture-twice \
            colour-primaries-twice colour-bad-luminance colour-surface-dead \
            screencopy-twice screencopy-not-shm screencopy-narrow screencopy-short \
            screencopy-thin-stride drag-source-reused; do
    "$IMWAY_CLIENT" "$mode" || { echo "the compositor let $mode through"; exit 1; }
    expect_alive "compositor died on $mode"
done

echo "OK: malformed clients were disconnected and compositor survived"
