#!/usr/bin/env bash
# The pointer over a mapped popup enters the popup's surface; a popup and a
# toplevel without content in the scene lists are passed over.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "popup mapped"
wait_rect 'app_id=pointer-popup '
x=$(dump_field 'app_id=pointer-popup ' imgx)
y=$(dump_field 'app_id=pointer-popup ' imgy)
px=$(dump_field '^popup mapped=1' x)
py=$(dump_field '^popup mapped=1' y)
[[ -n "$px" && -n "$py" ]] || { echo "no mapped popup in the dump"; dump_state; exit 1; }

# picking works from a rendered frame: keep aiming until the client says
# the popup has the pointer
for _ in $(seq 1 20); do
    # positions settle over the first frames: re-read them
    x=$(dump_field 'app_id=pointer-popup ' imgx)
    y=$(dump_field 'app_id=pointer-popup ' imgy)
    px=$(dump_field '^popup mapped=1' x)
    py=$(dump_field '^popup mapped=1' y)
    ctl "motion $((x + px + 50)) $((y + py + 40))"
    sleep 0.2
    ctl "motion $((x + px + 51)) $((y + py + 40))"
    grep -q "pointer on the popup" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "pointer on the popup"
expect_client_ok "the pointer never entered the popup"
echo "OK: the pointer entered the popup over its window"
