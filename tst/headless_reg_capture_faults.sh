#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="capture-submit=3 readback-fence=0"
# ext-image-copy-capture on a GPU that lets it down: a frame whose copy the
# queue refuses on all three frames it is retried on fails, a frame whose
# readback is lost with the device fails, and the frame after both lands.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_TESTS_BIN/client_reg_capture_edges" output-faults >"$XDG_RUNTIME_DIR/faults.log" 2>&1 || {
    echo "the output capture faults went wrong"
    cat "$XDG_RUNTIME_DIR/faults.log"
    exit 1
}
grep -q "output faults done" "$XDG_RUNTIME_DIR/faults.log" || { echo "the client did not finish"; cat "$XDG_RUNTIME_DIR/faults.log"; exit 1; }
[[ "$(grep -c "capture submit failed" "$IMWAY_LOG")" -eq 3 ]] || { echo "not three refused attempts"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: capture fence failed" || { echo "the lost readback was not reported"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on failed output captures"
echo "OK: refused and lost output captures fail their frames and the next one lands"
