#!/usr/bin/env bash
# Serial validation for interactive move: a garbage serial is ignored (the
# window must not follow the held drag), the real serial moves it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x0=$(dump_field 'app_id=serial' x); y0=$(dump_field 'app_id=serial' y)
imgx=$(dump_field 'app_id=serial' imgx); imgy=$(dump_field 'app_id=serial' imgy)
mx=$((imgx + 150)); my=$((imgy + 100))

# press #1: the client requests a move with a garbage serial, we drag held
ctl "motion $mx $my"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((mx + 1)) $my"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
wait_client "bad move requested"
for d in 15 30 45 60; do
    ctl "motion $((mx + d)) $my"
    sleep 0.1
done
ctl "button left release"
sleep 0.4

x1=$(dump_field 'app_id=serial' x)
echo "after bad-serial drag: x $x0 -> $x1"
(( x1 >= x0 - 2 && x1 <= x0 + 2 )) || { echo "BUG: stale serial moved the window"; exit 1; }

# press #2 at the same spot: real serial, the window must follow
ctl "motion $mx $my"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((mx + 1)) $my"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
wait_client "good move requested"
for d in 15 30 45 60; do
    ctl "motion $((mx + d)) $my"
    sleep 0.1
done
ctl "button left release"
sleep 0.4

x2=$(dump_field 'app_id=serial' x)
echo "after good-serial drag: x -> $x2"
(( x2 >= x0 + 40 )) || { echo "valid-serial move did not move the window"; exit 1; }
echo "OK: garbage serial ignored, valid serial moved the window"
