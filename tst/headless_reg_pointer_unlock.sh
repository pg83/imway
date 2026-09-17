#!/usr/bin/env bash
# A pointer confinement whose region is narrowed to nothing has nowhere left
# to hold the cursor, so the compositor ends it and says so.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
wait_rect 'app_id=pointer-unlock'

x=$(dump_field 'app_id=pointer-unlock' imgx)
y=$(dump_field 'app_id=pointer-unlock' imgy)
w=$(dump_field 'app_id=pointer-unlock' client_w)
h=$(dump_field 'app_id=pointer-unlock' client_h)

# pointer focus is worked out from a rendered frame, so keep aiming until
# the client has the confinement
got_it() { grep -q "confined" "$CLIENT_LOG"; }

for _ in $(seq 1 20); do
    ctl "motion $((x + w / 2)) $((y + h / 2))"
    sleep 0.2
    ctl "motion $((x + w / 2 + 1)) $((y + h / 2))"
    got_it && break
    sleep 0.3
done

wait_client "confined"
wait_client "unconfined"
expect_client_ok "the confinement did not end with its region"
expect_alive "compositor died ending a pointer confinement"
echo "OK: an empty region ends a pointer confinement"
