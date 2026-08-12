#!/usr/bin/env bash
# Interactive resize: dragging the ImGui left border runs the transactional
# configure/ack/commit resize. The right edge must stay pinned, the height
# must not change, and the client size must track the window delta exactly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "resize client mapped"
sleep 0.5 # let the SSD reconfigure and first frames settle
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x=$(dump_field 'app_id=resize' x);  y=$(dump_field 'app_id=resize' y)
w=$(dump_field 'app_id=resize' w);  h=$(dump_field 'app_id=resize' h)
cw=$(dump_field 'app_id=resize' client_w); ch=$(dump_field 'app_id=resize' client_h)
right=$((x + w))
echo "before: pos=$x,$y size=${w}x${h} client=${cw}x${ch}"

# hover the left border, let a frame see it, then press and drag 40px left
gy=$((y + h / 2))
ctl "motion $((x + 1)) $gy"
sleep 0.3
ctl "button left press"
sleep 0.3
for dx in 8 16 24 32 40; do
    ctl "motion $((x + 1 - dx)) $gy"
    sleep 0.15
done
ctl "button left release"
sleep 0.8 # drain the configure/commit transaction

nx=$(dump_field 'app_id=resize' x);  ny=$(dump_field 'app_id=resize' y)
nw=$(dump_field 'app_id=resize' w);  nh=$(dump_field 'app_id=resize' h)
ncw=$(dump_field 'app_id=resize' client_w); nch=$(dump_field 'app_id=resize' client_h)
nright=$((nx + nw))
echo "after: pos=$nx,$ny size=${nw}x${nh} client=${ncw}x${nch}"

kill -0 "$CLIENT_PID" 2>/dev/null || { echo "client died during resize"; cat "$CLIENT_LOG"; exit 1; }
(( nw >= w + 30 )) || { echo "window did not grow (w $w -> $nw)"; cat "$CLIENT_LOG"; exit 1; }
(( nright >= right - 2 && nright <= right + 2 )) || { echo "right edge moved: $right -> $nright"; exit 1; }
(( ncw - cw == nw - w )) || { echo "client delta +$((ncw - cw)) != window delta +$((nw - w))"; exit 1; }
(( nch == ch )) || { echo "height changed on a horizontal drag: $ch -> $nch"; exit 1; }
(( ny == y )) || { echo "y moved on a horizontal drag: $y -> $ny"; exit 1; }
echo "OK: left-edge drag grew the window leftwards with the right edge pinned"
