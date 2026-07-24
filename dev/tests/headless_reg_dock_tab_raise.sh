#!/usr/bin/env bash
# Raising a window that sits in an unselected dock tab (alt-tab here; the
# taskbar click and xdg-activation arm the same raiseRequested) must select
# its tab and wake it: before the fix the request was only consumed inside a
# successful ImGui::Begin, which a hidden tab never reaches.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

cbs() { grep -c '^cb ' "$CLIENT_LOG" || true; }
cbs_at_least() { [[ "$(cbs)" -ge "$1" ]]; }

start_client
wait_client "two mapped"
await 100 cbs_at_least 20 || { echo "no frame callbacks while visible"; exit 1; }

ax=$(dump_field 'app_id=dock-anim' x); ay=$(dump_field 'app_id=dock-anim' y)
aw=$(dump_field 'app_id=dock-anim' w); ah=$(dump_field 'app_id=dock-anim' h)
bx=$(dump_field 'app_id=dock-static' x); by=$(dump_field 'app_id=dock-static' y)
bw=$(dump_field 'app_id=dock-static' w)

# tab them into one node: dock-static selected, dock-anim hidden behind it
sx=$((bx + bw / 2)); sy=$((by + 10))
tx=$((ax + aw / 2)); ty=$((ay + ah / 2))
ctl "motion $sx $sy"
sleep 0.3
ctl "button left press"
sleep 0.3
for f in 4 3 2 1; do
    ctl "motion $((tx + (sx - tx) * f / 5)) $((ty + (sy - ty) * f / 5))"
    sleep 0.15
done
ctl "motion $tx $ty"
sleep 0.6
ctl "button left release"
sleep 0.5

[[ "$(dump_field 'app_id=dock-static' docked)" == 1 ]] || {
    echo "docking drag did not land"; dump_state; exit 1; }

# the hidden animator quiesces (covered in depth by the sibling reg test)
sleep 1
c0=$(cbs)

# alt-tab: one step away from dock-static lands on dock-anim, releasing alt
# commits the raise
ctl "key 56 press"
sleep 0.2
ctl "key 15 press"
ctl "key 15 release"
sleep 0.3
ctl "key 56 release"
sleep 0.3

await 100 cbs_at_least $((c0 + 10)) || {
    echo "alt-tab did not surface the tabbed-behind window"; dump_state; exit 1; }

[[ "$(dump_field 'app_id=dock-anim' focused)" == 1 ]] || {
    echo "raised window did not take focus"; dump_state; exit 1; }

expect_alive
echo "OK: alt-tab selects the hidden dock tab and wakes its client"
