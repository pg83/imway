#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# zwlr-screencopy while a fullscreen client is on the primary plane: that
# frame was never composed, so there is nothing to read back. The copy
# forces one composed frame and delivers it, the client's red, and the copy
# after an output mode change is the whole frame at the new size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain"; cat "$IMWAY_LOG"; exit 1; }
copier="$IMWAY_TESTS_BIN/client_reg_screencopy_bare"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_direct_scanout"
start_client
wait_client "taint candidate mapped"
tlid=$(dump_field 'title=kms-taint' id)
candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
await 100 candidate || { echo "the client never reached the plane"; dump_state; exit 1; }

"$copier" "$XDG_RUNTIME_DIR/direct.ppm" || { echo "the copy of a direct-scanout frame failed"; cat "$IMWAY_LOG"; exit 1; }
python3 - "$XDG_RUNTIME_DIR/direct.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
red = sum(1 for i in range(0, len(d), 3 * 97) if d[i] > 200 and d[i + 1] < 60 and d[i + 2] < 60)
total = len(d) // (3 * 97)
print(f"{w}x{h}: red {red} of {total}")
assert (w, h) == (1280, 800), "the copy is not the output's size"
assert red > total * .8, "the copy does not show the fullscreen client"
PY

kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }
ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || { echo "the new mode was not taken"; cat "$IMWAY_LOG"; exit 1; }
flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips at the new mode"; exit 1; }

"$copier" "$XDG_RUNTIME_DIR/large.ppm" || { echo "the copy after the mode change failed"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/large.ppm")" == 1920x1080 ]] || {
    echo "the copy after the mode change is not 1920x1080"
    exit 1
}

expect_alive "compositor died copying a direct-scanout frame"
echo "OK: screencopy composes a direct-scanout frame and follows a mode change"
