#!/usr/bin/env bash
# Unmap in the middle of a border resize: the window vanishes under the
# held drag, the compositor survives, and the remapped window resizes again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "resize client mapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x=$(dump_field 'app_id=rsunmap' x); y=$(dump_field 'app_id=rsunmap' y)
w=$(dump_field 'app_id=rsunmap' w); h=$(dump_field 'app_id=rsunmap' h)
gx=$((x + w - 1)); gy=$((y + h / 2))

# grab the right border and start dragging
ctl "motion $gx $gy"
sleep 0.3
ctl "button left press"
sleep 0.3
ctl "motion $((gx + 20)) $gy"
sleep 0.2
ctl "motion $((gx + 40)) $gy"
sleep 0.2

# unmap under the held drag
ctl "key 22 press"; ctl "key 22 release"   # KEY_U
wait_client "unmapped"
sleep 0.3
ctl "motion $((gx + 60)) $gy"
sleep 0.2
ctl "button left release"
sleep 0.3
expect_alive "compositor died when the window unmapped under an active resize"

# remap and resize again
ctl "key 19 press"; ctl "key 19 release"   # KEY_R
wait_client "remapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x=$(dump_field 'app_id=rsunmap' x); y=$(dump_field 'app_id=rsunmap' y)
w=$(dump_field 'app_id=rsunmap' w); h=$(dump_field 'app_id=rsunmap' h)
cw=$(dump_field 'app_id=rsunmap' client_w)
gx=$((x + w - 1)); gy=$((y + h / 2))
ctl "motion $gx $gy"
sleep 0.3
ctl "button left press"
sleep 0.3
for d in 15 30 45; do
    ctl "motion $((gx + d)) $gy"
    sleep 0.15
done
sleep 0.6
ctl "button left release"
sleep 0.5

ncw=$(dump_field 'app_id=rsunmap' client_w)
echo "post-remap resize: client_w $cw -> $ncw"
(( ncw > cw + 20 )) || { echo "remapped window no longer resizes"; exit 1; }
expect_alive "compositor died on the post-remap resize"
echo "OK: unmap under an active resize is clean, the remapped window resizes"
