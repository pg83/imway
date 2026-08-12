#!/usr/bin/env bash
# imway-args: --hdr 203
# Current color-management-v1 protocol contract, independent of rendering.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" info-hdr
"$IMWAY_CLIENT" pq-defaults
"$IMWAY_CLIENT" output-resource-lifetime
"$IMWAY_CLIENT" feedback-inert
expect_alive "compositor died during color protocol checks"

start_client changes
wait_client "color-protocol: waiting for change"
ctl "sdr-white 120"
expect_client_ok "color change client failed"
grep -q "color-protocol: changes ok" "$CLIENT_LOG"

echo "OK: current color-management protocol contract"
