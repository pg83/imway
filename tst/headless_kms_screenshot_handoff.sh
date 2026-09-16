#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHILD_LOG=./viewer.log
# imway-args: --device auto
# The screenshot chord on a KMS session hands the scanout buffer itself to
# the viewer instead of reading pixels back: the compositor swaps in a
# replacement scanout, the old one travels as a dma-buf, and the viewer
# imports it and encodes the PNG.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name handoff"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

# the fullscreen dmabuf client of the direct-scanout scenario
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_direct_scanout"
start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)
candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
await 100 candidate || { echo "the client never reached the plane"; dump_state; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print

await 100 in_log "screenshot handoff of the scanout buffer" || {
    echo "the capture was read back instead of handed off"
    cat "$IMWAY_LOG"
    exit 1
}

saved() {
    [[ -s "$shots/handoff.png" ]]
}
viewer_failed() {
    in_log "exited with status 1"
}

for _ in $(seq 1 200); do
    saved && break
    viewer_failed && break
    sleep 0.1
done

saved || {
    echo "the handed-off capture produced no file; the viewer said:"
    cat "$XDG_RUNTIME_DIR/viewer.log" 2>/dev/null || echo "(no viewer log)"
    exit 1
}

[[ "$(head -c 4 "$shots/handoff.png" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || {
    echo "handoff.png is not a PNG"; exit 1; }
await 100 in_log "exited with status 0" || { echo "the viewer did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }

# the session keeps flipping on its replacement scanout
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "flips stopped after the handoff"; exit 1; }

expect_alive "compositor died handing off a scanout"
echo "OK: the scanout buffer itself reaches the viewer and encodes"
