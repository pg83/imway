#!/usr/bin/env bash
# xdg_output objects of every manager version follow a live mode change:
# the new logical size, closed by xdg_output.done before v3 and by
# wl_output.done from v3 on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "xdg outputs 1280x800"

# the connector stays up; only the mode list changes under the probe
ctl "kms-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || { echo "the mode change was not followed"; cat "$IMWAY_LOG"; exit 1; }

wait_client "xdg output resize done"
expect_client_ok "an xdg_output missed the new size or its done"
echo "OK: xdg outputs of every version followed the mode change"
