#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --mode 1920x1080
# A floating window parked at the far corner of a 1080p display must stay
# reachable when the display shrinks to 800p: enough of it — including
# the titlebar — has to remain on screen to grab.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1920x1080@60" || { echo "no 1080p boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "resize client mapped"
sleep 0.5

x=$(dump_field 'app_id=resize' x); y=$(dump_field 'app_id=resize' y)
w=$(dump_field 'app_id=resize' w)

# drag by the titlebar to the bottom-right corner
gx=$((x + w / 2)); gy=$((y + 10))
ctl "motion $gx $gy"
sleep 0.3
ctl "button left press"
sleep 0.3
for step in 1 2 3 4 5; do
    ctl "motion $((gx + (1650 - x) * step / 5)) $((gy + (900 - y) * step / 5))"
    sleep 0.15
done
ctl "button left release"
sleep 0.5

px=$(dump_field 'app_id=resize' x); py=$(dump_field 'app_id=resize' y)
echo "parked at $px,$py"
[[ "$px" -gt 1300 ]] || { echo "drag failed, window at $px,$py"; dump_state; exit 1; }

# the display shrinks to 800p
ctl "kms-connector 0"
ctl "kms-modes 2"
ctl "kms-connector 1"
await 100 in_log "kms output: 1280x800@60" || { echo "shrink not taken"; cat "$IMWAY_LOG"; exit 1; }

# let a frame lay the desktop out at the new size
ctl "key 2 press"; ctl "key 2 release"
sleep 0.5

nx=$(dump_field 'app_id=resize' x); ny=$(dump_field 'app_id=resize' y)
echo "after shrink at $nx,$ny"

# the layout clamp must leave a visible slice on screen...
[[ "$nx" -lt $((1280 - 8)) ]] || { echo "window lost beyond the right edge ($nx)"; exit 1; }
[[ "$ny" -lt $((800 - 8)) ]] || { echo "window lost beyond the bottom edge ($ny)"; exit 1; }

# ...and reachable means grabbable: drag it back inside by that slice
ctl "motion $((nx + 8)) $((ny + 10))"
sleep 0.3
ctl "button left press"
sleep 0.3
for step in 1 2 3 4 5; do
    ctl "motion $((nx + 8 - (nx - 600) * step / 5)) $((ny + 10 - (ny - 300) * step / 5))"
    sleep 0.15
done
ctl "button left release"
sleep 0.5

bx=$(dump_field 'app_id=resize' x); by=$(dump_field 'app_id=resize' y)
echo "pulled back to $bx,$by"
[[ "$bx" -lt $((nx - 200)) && "$by" -lt "$ny" ]] || {
    echo "window could not be grabbed after the shrink ($nx,$ny -> $bx,$by)"
    exit 1
}

kill -0 "$CLIENT_PID" || { echo "client died across the shrink"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died shrinking under a floating window"
echo "OK: the floating window stays reachable after the shrink"
