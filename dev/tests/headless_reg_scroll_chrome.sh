#!/usr/bin/env bash
# Scroll routing: wheel over client content reaches the client, wheel over
# the top menu bar stays with the compositor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

imgx=$(dump_field 'app_id=scroll' imgx); imgy=$(dump_field 'app_id=scroll' imgy)

# phase A: over the content
ctl "motion $((imgx + 150)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 151)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "scroll 1"
wait_client "phaseA ok"

# phase B: over the menu bar (top strip of the screen)
ctl "motion 640 8"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion 641 8"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "scroll 1"
ctl "scroll 1"
sleep 0.3
ctl "key 50 press"; ctl "key 50 release"   # KEY_M sentinel

expect_client_ok "scroll routing broken"
echo "OK: content scroll delivered, menu-bar scroll stayed with the chrome"
