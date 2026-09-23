#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=readback-busy=1000
# A zwlr-screencopy frame whose wl_buffer is destroyed while its readback is
# on the GPU (the fence reads busy for a thousand polls) fails once the
# readback lands, having nowhere to copy to.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_capture_abandon"
start_client wlr-orphan
wait_client "bufferless screencopy failed"
expect_client_ok "the orphaned copy did not fail"
expect_alive "compositor died finishing an orphaned copy's readback"
echo "OK: a screencopy whose buffer went in flight fails"
