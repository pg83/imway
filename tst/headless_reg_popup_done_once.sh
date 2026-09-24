#!/usr/bin/env bash
# popup_done comes once a popup: a child popup dismissed at its grab gets
# none more when its parent's tree is dismissed by the window unmapping.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "popup-done-once mapped"
wait_mapped

ctl "key 30 press"
ctl "key 30 release"

wait_client "popup_done came once each"
expect_client_ok "a popup was dismissed twice or never"
expect_alive "compositor died dismissing a popup tree"
echo "OK: a dismissed popup is not dismissed again with its parent's tree"
