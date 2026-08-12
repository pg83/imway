#!/usr/bin/env bash
# Double wl_seat binding: both bindings receive input; releasing the first
# pointer leaves the second working.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

imgx=$(dump_field 'app_id=seat2' imgx); imgy=$(dump_field 'app_id=seat2' imgy)
click_at $((imgx + 150)) $((imgy + 100))
ctl "key 30 press"; ctl "key 30 release"    # client watches KEY_A by default? no: set below
wait_client "both bindings saw input"
wait_client "first pointer released"

click_at $((imgx + 150)) $((imgy + 100))
expect_client_ok "second binding lost input after releasing the first"
echo "OK: double seat binding fans out input; release of one leaves the other"
