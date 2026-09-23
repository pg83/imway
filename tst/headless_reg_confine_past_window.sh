#!/usr/bin/env bash
# A confine region reaching past the window's top-left corner, plus a
# rectangle wholly beside the window: the confinement is the part inside the
# window, so pushed up and left the pointer stops at the window's own corner
# and pushed down and right at the rectangle's inner edges.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_rect 'app_id=confine-past'

for _ in $(seq 20); do
    x=$(dump_field 'app_id=confine-past' imgx); y=$(dump_field 'app_id=confine-past' imgy)
    ctl "motion $((x + 50)) $((y + 40))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((x + 51)) $((y + 40))"
    await 10 grep -q "^confined" "$CLIENT_LOG" && break
done
wait_client "confined"

motions() { grep -c "^motion" "$CLIENT_LOG" || true; }
push() { # <dx> <dy> <times>
    local i n
    for ((i = 0; i < $3; i++)); do
        n=$(motions)
        ctl "relmotion $1 $2"
        moved() { (( $(motions) > n )); }
        await 30 moved || true
    done
}
saw() { grep -qE "^motion $1" "$CLIENT_LOG"; }

push -40 -40 4
await 30 saw "0\\.0 0\\.0" || { echo "the pointer did not stop at the window's corner"; cat "$CLIENT_LOG"; exit 1; }
push 40 40 6
await 30 saw "99\\.0 79\\.0" || { echo "the pointer did not stop at the region's inner corner"; cat "$CLIENT_LOG"; exit 1; }
! grep -q "escaped" "$CLIENT_LOG" || { echo "a motion left the part of the region inside the window"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died confining to a region past the window"
echo "OK: a region past the window confines to its part inside"
