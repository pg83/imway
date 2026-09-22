#!/usr/bin/env bash
# imway-env: IMWAY_DRI_DIR=./nodri
# imway-pre: mkdir -p nodri
# A headless compositor on a host with no drm node at all (a container, a
# VM without a virtual GPU): it still serves clients, but offers nothing
# that needs a node behind it — no lease device, no explicit sync — and
# names a dmabuf main device only when the renderer knows its own.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_headless_drm"
start_client none
wait_client "no drm node"
expect_client_ok "the globals promised a drm node that is not there"

expect_alive "compositor died without a drm node"
echo "OK: no drm node, no drm-backed globals"
