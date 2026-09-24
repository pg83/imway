#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_10BIT=1
# An 8-bit scanout: the frame capture hands its rows over as they are,
# with no 10-bit collapse, and a raw screenshot is the 8-bit PPM the
# framebuffer holds. The copied window keeps its exact magenta.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "10-bit scanout" && { echo "the plane without 10-bit formats scanned out 10 bits"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_screencopy"
start_client
wait_client "screencopy done"
expect_client_ok "an 8-bit capture lost the window's colour"

screenshot_raw "$XDG_RUNTIME_DIR/raw.ppm"
[[ "$(sed -n 3p "$XDG_RUNTIME_DIR/raw.ppm")" == 255 ]] || { echo "the raw screenshot of an 8-bit framebuffer is not 8-bit"; exit 1; }

expect_alive "compositor died capturing an 8-bit scanout"
echo "OK: an 8-bit scanout is captured and screenshot as it is"
