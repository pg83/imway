#!/usr/bin/env bash
# Confinement to a region of two disjoint rectangles: pushed past the top
# and the bottom the pointer stops at the band's edges, a short push toward
# the gap stops at the first rectangle's edge (it is the nearer one), and a
# push that lands nearer the second rectangle moves the pointer into it.
# No motion the client sees lies outside the region.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
wait_rect 'app_id=confine-band'

# into the first rectangle, re-aimed until the client is confined
for _ in $(seq 20); do
    x=$(dump_field 'app_id=confine-band' imgx); y=$(dump_field 'app_id=confine-band' imgy)
    ctl "motion $((x + 70)) $((y + 100))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((x + 71)) $((y + 100))"
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

push 0 -40 4
await 30 saw "[0-9.]+ 60\.0" || { echo "the pointer did not stop at the band's top"; cat "$CLIENT_LOG"; exit 1; }
push 0 40 6
await 30 saw "[0-9.]+ 139\.0" || { echo "the pointer did not stop at the band's bottom"; cat "$CLIENT_LOG"; exit 1; }
push 30 0 4
await 30 saw "119\.0 " || { echo "a short push did not stop at the first rectangle's edge"; cat "$CLIENT_LOG"; exit 1; }
push 70 0 1
await 30 saw "(1[89][0-9]|2[0-7][0-9])\.[0-9] " || { echo "a push nearer the second rectangle did not reach it"; cat "$CLIENT_LOG"; exit 1; }

! grep -q "escaped" "$CLIENT_LOG" || { echo "a motion left the confine region"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died confining to two rectangles"
echo "OK: confinement to two rectangles clamps to the nearer one"
