#!/usr/bin/env bash
# The pointer over a below-stacked subsurface, in a corner its parent leaves
# out of its input region, enters the subsurface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "pointer below: mapped"

# the window rect is per-frame truth and picking needs a rendered frame
# after the motion: re-read and re-aim until the client reports it
for _ in $(seq 1 20); do
    x=$(dump_field 'app_id=pointer-below' imgx)
    y=$(dump_field 'app_id=pointer-below' imgy)
    if [[ -n "$x" && -n "$y" ]]; then
        ctl "motion $((x + 175)) $((y + 175))"
        sleep 0.2
        ctl "motion $((x + 176)) $((y + 175))"
    fi
    grep -q "pointer on the lower subsurface" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "pointer on the lower subsurface"
expect_client_ok "the pointer never entered the lower subsurface"
echo "OK: the pointer entered the below-stacked subsurface where it shows"
