#!/usr/bin/env bash
# DnD interrupted mid-operation: the source resource dies during the drag
# (input keeps flowing after), and a never-accepted drag is cancelled on
# release instead of dropped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# round 1: destroy the source resource mid-drag
start_client destroy-source
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
imgx=$(dump_field 'app_id=dndphase' imgx); imgy=$(dump_field 'app_id=dndphase' imgy)
ctl "motion $((imgx + 150)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 151)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
wait_client "dragging"
ctl "relmotion 5 5"
sleep 0.2
ctl "key 2 press"; ctl "key 2 release"   # KEY_1: destroy the source
wait_client "source destroyed"
expect_alive "compositor died when the drag source resource was destroyed"
ctl "button left release"
sleep 0.3
click_at $((imgx + 150)) $((imgy + 100))
expect_client_ok "input dead after mid-drag source destruction"
kill "$CLIENT_PID" 2>/dev/null || true
sleep 0.3

# round 2: drag over our own surface, never accept, release -> cancelled
start_client no-accept
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
imgx=$(dump_field 'app_id=dndphase' imgx); imgy=$(dump_field 'app_id=dndphase' imgy)
ctl "motion $((imgx + 150)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((imgx + 151)) $((imgy + 100))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
wait_client "dragging"
# a nudge keeps the drag over our surface so it enters as the target
ctl "motion $((imgx + 155)) $((imgy + 102))"
sleep 0.2
wait_client "entered"
ctl "button left release"
expect_client_ok "unaccepted drag was not cancelled"
expect_alive "compositor died on an unaccepted drop"
echo "OK: mid-drag source death is clean, unaccepted drops cancel"
