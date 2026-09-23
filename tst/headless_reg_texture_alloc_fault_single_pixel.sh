#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=client-texture=0
# A single-pixel buffer is uploaded through a 1x1 texture of its own; the
# device cannot create it: the owner is disconnected as a render fault,
# nothing is drawn from the missing texture, and the session goes on
# presenting for everyone else.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_single_pixel"
start_client
wait_client "committed 1x1"

await 100 in_log "render fault, disconnecting client_feat_single_pixel" || {
    echo "the owner of the failed texture was not disconnected"
    cat "$IMWAY_LOG"
    exit 1
}

in_log "texture allocation failed 1x1" || {
    echo "the failed texture did not report"
    cat "$IMWAY_LOG"
    exit 1
}

expect_client_ok "the faulted client did not see its disconnect"

"$(dirname "$IMWAY_CLIENT")/client_health_probe" || {
    echo "the output stopped presenting after the render fault"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on a failed single-pixel texture"
echo "OK: a failed single-pixel texture faults its owner only"
