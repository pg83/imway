#!/usr/bin/env bash
# The pointer jumps onto a window in one motion and rests there, then jumps
# off to the bare desktop in one motion and rests again. The pick of a motion
# can only see what the last composed frame had under the pointer, so each
# jump is settled at a frame edge after it: the window hears its enter and
# then its leave with no further motion to prompt either. So is a release
# that ends an implicit grab off the window, and a window that maps over
# the resting pointer takes it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "pointer-rest mapped"
wait_mapped 'app_id=pointer-rest'
wait_placed 'app_id=pointer-rest' || { echo "the window never settled"; dump_state; exit 1; }

x=$(dump_field 'app_id=pointer-rest' imgx); y=$(dump_field 'app_id=pointer-rest' imgy)
w=$(dump_field 'app_id=pointer-rest' client_w); h=$(dump_field 'app_id=pointer-rest' client_h)

# start well off the window, and let a frame settle there
ctl "motion 1200 700"
compose_frame
compose_frame

entered() { grep -c "pointer entered" "$CLIENT_LOG" || true; }
left() { grep -c "pointer left" "$CLIENT_LOG" || true; }
[[ "$(entered)" == 0 ]] || { echo "the window had the pointer before it came"; cat "$CLIENT_LOG"; exit 1; }

ctl "motion $((x + w / 2)) $((y + h / 2))"
compose_frame
compose_frame
has_entered() { [[ "$(entered)" == 1 ]]; }
await 50 has_entered || { echo "the pointer resting on the window after one jump never entered it"; cat "$CLIENT_LOG"; exit 1; }

ctl "motion 1200 700"
compose_frame
compose_frame
has_left() { [[ "$(left)" == 1 ]]; }
await 50 has_left || { echo "the pointer resting on the desktop after one jump never left the window"; cat "$CLIENT_LOG"; exit 1; }

# a press on the window holds its implicit grab while the pointer is
# dragged off it; released out there, the pointer rests on the desktop and
# the window hears its leave without another motion
ctl "motion $((x + w / 2)) $((y + h / 2))"
compose_frame
compose_frame
entered_again() { [[ "$(entered)" == 2 ]]; }
await 50 entered_again || { echo "the pointer back on the window did not enter it"; cat "$CLIENT_LOG"; exit 1; }
ctl "button left press"
ctl "motion 1200 700"
compose_frame
compose_frame
[[ "$(left)" == 1 ]] || { echo "the implicit grab let the window go while the button was held"; cat "$CLIENT_LOG"; exit 1; }
ctl "button left release"
compose_frame
compose_frame
left_again() { [[ "$(left)" == 2 ]]; }
await 50 left_again || { echo "the pointer released off the window never left it"; cat "$CLIENT_LOG"; exit 1; }

# resting on the first window where a cascaded second one will cover it,
# the pointer goes to the second as it maps over the spot
px=$((x + w - 20)); py=$((y + h - 20))
ctl "motion $px $py"
compose_frame
compose_frame
entered_third() { [[ "$(entered)" == 3 ]]; }
await 50 entered_third || { echo "the pointer back on the first window did not enter it"; cat "$CLIENT_LOG"; exit 1; }
touch go-second
wait_client "second mapped"
wait_mapped 'app_id=pointer-rest-second'
sx=$(dump_field 'app_id=pointer-rest-second' imgx); sy=$(dump_field 'app_id=pointer-rest-second' imgy)
(( px >= sx && py >= sy )) || { echo "the second window was placed off the resting pointer (${sx},${sy} vs ${px},${py})"; exit 1; }
second_entered() { grep -q "pointer entered second" "$CLIENT_LOG"; }
await 50 second_entered || { echo "the window that mapped under the resting pointer never got it"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died settling a resting pointer"
echo "OK: a pointer resting after a jump or a release, or under a window mapping over it, enters and leaves at the next frame edge"
