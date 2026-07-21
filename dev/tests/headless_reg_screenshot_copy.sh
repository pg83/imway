#!/usr/bin/env bash
# imway-args: --hdr 203
# Copy from the screenshot cropper must install image/jxl and the image/png
# compatibility selection, then exit after another focused client replaces it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "copy receiver ready"

ctl "key 99 press"
ctl "key 99 release"
await 100 in_log "toplevel imway screenshot () mapped"
sleep 0.5

ctl "key 29 press"   # left ctrl
sleep 0.1
ctl "key 46 press"   # C
sleep 0.2
ctl "key 46 release"
ctl "key 29 release"

expect_client_ok "screenshot Copy did not publish JXL and PNG"
await 100 in_log "toplevel imway screenshot destroyed" || {
    echo "hidden screenshot owner did not exit after replacement"
    exit 1
}
expect_alive "screenshot Copy killed the compositor"
echo "OK: screenshot Copy published JXL and PNG and released ownership cleanly"
