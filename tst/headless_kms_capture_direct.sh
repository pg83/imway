#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# ext-image-copy-capture of the output while a fullscreen client is on the
# primary plane: that frame was never composed, so the first capture attempt
# asks for a composed frame and the retry delivers it - the client's red.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_direct_scanout"
start_client
wait_client "taint candidate mapped"
tlid=$(dump_field 'title=kms-taint' id)
candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}
await 100 candidate || { echo "the client never reached the plane"; dump_state; exit 1; }

"$IMWAY_TESTS_BIN/client_capture_output" "$XDG_RUNTIME_DIR/direct.ppm" || { echo "the capture of a direct-scanout frame failed"; cat "$IMWAY_LOG"; exit 1; }
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
assert red > total * .8, "the capture does not show the fullscreen client"
PY
expect_alive "compositor died capturing a direct-scanout frame"
echo "OK: output capture composes a direct-scanout frame"
