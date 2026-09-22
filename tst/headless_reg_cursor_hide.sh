#!/usr/bin/env bash
# wl_pointer.set_cursor with a null surface hides the cursor: the scene's
# cursor is the hidden shape with no client surface behind it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

hidden=37 # CursorKind::hidden

hidden_now() {
    [[ "$(dump_field '^cursor ' shape) $(dump_field '^cursor ' surface)" == "$hidden 0" ]]
}

start_client
wait_client "cursor hide mapped"
wait_rect 'app_id=cursor-hide'
x=$(dump_field 'app_id=cursor-hide' imgx)
y=$(dump_field 'app_id=cursor-hide' imgy)

# pointer focus is worked out from a rendered frame: keep aiming until the
# client got its enter
for _ in $(seq 1 20); do
    # the window position settles over the first frames: re-read it
    x=$(dump_field 'app_id=cursor-hide' imgx)
    y=$(dump_field 'app_id=cursor-hide' imgy)
    ctl "motion $((x + 40)) $((y + 40))"
    sleep 0.2
    ctl "motion $((x + 41)) $((y + 40))"
    grep -q "cursor hidden sent" "$CLIENT_LOG" && break
    sleep 0.3
done

wait_client "cursor hidden sent"

await 50 hidden_now || {
    echo "a null cursor surface did not hide the cursor"
    dump_state | grep '^cursor'
    exit 1
}

expect_alive "compositor died hiding the cursor"
echo "OK: a null set_cursor hid the cursor"
