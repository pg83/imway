#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The KMS path on the userspace emulator: modeset against the fake
# connector, a zero-copy scanout swapchain on the real GPU, page flips
# paced by the emulator's event pipe, and a client composited end to end.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60, connector 101, crtc 103, plane 104" || {
    echo "modeset did not reach the fake connector"
    cat "$IMWAY_LOG"
    exit 1
}

in_log "scanout swapchain" || { echo "no zero-copy swapchain"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "kms smoke mapped"

# the full render path works: the client's color survives into the readback
screenshot "$XDG_RUNTIME_DIR/kms.ppm"
read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/kms.ppm" 'title=kms-smoke')
[[ "$b" -gt 100 && "$b" -gt $((r + 30)) ]] || {
    echo "client color did not survive the kms render path ($r $g $b)"
    exit 1
}

# flip pacing: the frame-callback spinner may only run at the emulator's
# vblank cadence, one frame per flip event
frames() { dump_state | awk '/^frames/ { print $2 }' | cut -d= -f2; }

f0=$(frames)
sleep 2
f1=$(frames)
rate=$(( (f1 - f0) / 2 ))

[[ "$rate" -ge 40 && "$rate" -le 80 ]] || {
    echo "flip pacing off: $rate fps (frames $f0 -> $f1)"
    exit 1
}

expect_alive "compositor died on the fake kms"
echo "OK: modeset, zero-copy swapchain and ~60Hz flips on the userspace kms"
