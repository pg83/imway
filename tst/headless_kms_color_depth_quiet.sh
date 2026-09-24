#!/usr/bin/env bash
# imway-args: --bpc 12
# A link that comes back shallower than asked for (12 bpc asked, the
# display takes 10) and an RGB range changed live: neither changes the
# colour an image description describes, so a colour-managed client is
# told of no new description.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "changes after boot"
ctl "set display.range 1"
await 50 in_log "fake-kms: Broadcast RGB = 1" || { echo "the range did not reach the connector"; cat "$IMWAY_LOG"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-range"
wait_client "changes after range"
expect_client_ok "the output's description changed with its link depth or range"
expect_alive "compositor died on a link depth or range change"
echo "OK: link depth and range leave the image description alone"
