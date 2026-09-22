#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHILD_LOG=./viewer.log
# imway-args: --device auto
# The screenshot chord on a KMS session hands the scanout buffer itself to
# the viewer instead of reading pixels back: the compositor swaps in a
# replacement scanout and the old one travels to the viewer as a dma-buf.
# The viewer finds the exporting GPU by its deviceUUID, which a software
# device without a drm node has as well, imports the buffer and encodes the
# PNG from it. Only a device without the import extensions may refuse, and
# must say so; the compositor keeps running either way.
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
viewer_spoke() {
    [[ -s "$XDG_RUNTIME_DIR/viewer.log" ]]
}

for _ in $(seq 1 200); do
    saved && break
    viewer_spoke && break
    sleep 0.1
done

if saved; then
    [[ "$(head -c 4 "$shots/handoff.png" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || {
        echo "handoff.png is not a PNG"; exit 1; }
    await 100 in_log "exited with status 0" || { echo "the viewer did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }
else
    viewer_spoke || { echo "the viewer neither encoded nor reported anything"; cat "$IMWAY_LOG"; exit 1; }
    grep -q "vulkan cannot import shared screenshot" "$XDG_RUNTIME_DIR/viewer.log" || {
        echo "the viewer failed to import the shared buffer:"
        cat "$XDG_RUNTIME_DIR/viewer.log"
        exit 1
    }
    echo "note: this device has no dma-buf image import, the viewer reported it"
fi

# the session keeps flipping on its replacement scanout: with the client
# gone the desktop is composited again, round the whole scanout ring, into
# the replacement as well
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -ge "$((f0 + 8))" ]]; }
for i in $(seq 1 100); do
    advanced && break
    ctl "motion $((100 + i * 3)) 200"
    sleep 0.05
done
advanced || { echo "flips stopped after the handoff"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/after.ppm"
python3 - "$XDG_RUNTIME_DIR/after.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
lit = sum(1 for i in range(0, len(d), 3 * 17) if d[i] + d[i + 1] + d[i + 2] > 30)
print(f"lit samples={lit} of {len(d) // (3 * 17)}")
assert lit > len(d) // (3 * 17) // 4, "the desktop composited after the handoff is black"
PY

expect_alive "compositor died handing off a scanout"
echo "OK: the scanout buffer itself reaches the viewer and encodes"
