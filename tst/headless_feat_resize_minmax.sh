#!/usr/bin/env bash
# Interactive resize against min/max size: dragging far past the max clamps
# the client at 400, dragging far past the min clamps at 200.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# drag the RIGHT border (the left one runs into the screen edge at x=0)
drag_right_border() { # <dx>
    local x y w h gx gy
    x=$(dump_field 'app_id=minmax' x); y=$(dump_field 'app_id=minmax' y)
    w=$(dump_field 'app_id=minmax' w); h=$(dump_field 'app_id=minmax' h)
    gx=$((x + w - 1))
    gy=$((y + h / 2))
    ctl "motion $gx $gy"
    sleep 0.3
    ctl "button left press"
    sleep 0.3
    local step target=$1
    for step in 1 2 3 4 5; do
        ctl "motion $((gx + target * step / 5)) $gy"
        sleep 0.15
    done
    # the transactional resize trails the pointer at the client's pace and
    # drops whatever is still in flight at release — hold until it settles
    local prev=-1 cur
    for step in $(seq 1 30); do
        cur=$(dump_field 'app_id=minmax' client_w)
        [[ "$cur" == "$prev" ]] && break
        prev=$cur
        sleep 0.25
    done
    ctl "button left release"
    sleep 0.5
}

start_client
wait_client "minmax client mapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

cw=$(dump_field 'app_id=minmax' client_w)
(( cw == 300 )) || { echo "unexpected initial width $cw"; exit 1; }

# 200px outward: 300 -> 500 wanted, must clamp at max 400
drag_right_border 200
cw=$(dump_field 'app_id=minmax' client_w)
echo "after outward drag: client_w=$cw"
(( cw == 400 )) || { echo "max clamp broken: client_w=$cw, want 400"; exit 1; }

# 300px inward: 400 -> 100 wanted, must clamp at min 200
drag_right_border -300
cw=$(dump_field 'app_id=minmax' client_w)
echo "after inward drag: client_w=$cw"
(( cw == 200 )) || { echo "min clamp broken: client_w=$cw, want 200"; exit 1; }

ch=$(dump_field 'app_id=minmax' client_h)
(( ch == 200 )) || { echo "height drifted on horizontal drags: $ch"; exit 1; }
echo "OK: interactive resize clamped at max 400 and min 200"
