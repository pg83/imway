#!/usr/bin/env bash
# A window tabbed behind another in an imgui dock node is not visible: its
# frame callbacks must stop, like a minimized window's, instead of pacing the
# hidden client at full rate — and resume when its tab is selected again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

cbs() { grep -c '^cb ' "$CLIENT_LOG" || true; }
cbs_at_least() { [[ "$(cbs)" -ge "$1" ]]; }

start_client
wait_client "two mapped"

# the animator renders freely while its window is visible
await 100 cbs_at_least 20 || { echo "no frame callbacks while visible"; exit 1; }

ax=$(dump_field 'app_id=dock-anim' x); ay=$(dump_field 'app_id=dock-anim' y)
aw=$(dump_field 'app_id=dock-anim' w); ah=$(dump_field 'app_id=dock-anim' h)
bx=$(dump_field 'app_id=dock-static' x); by=$(dump_field 'app_id=dock-static' y)
bw=$(dump_field 'app_id=dock-static' w)

# drag dock-static by its title bar onto dock-anim's center: the central
# drop box tabs them into one node with dock-static as the selected tab
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

# the tab behind the selected one must go quiet
sleep 0.5
c0=$(cbs)
sleep 1.5
c1=$(cbs)
(( c1 - c0 <= 5 )) || {
    echo "hidden dock tab still gets frame callbacks ($((c1 - c0)) in 1.5s)"
    exit 1
}

# selecting the tab brings the window back and its callbacks with it
nx=$(dump_field 'app_id=dock-static' x); ny=$(dump_field 'app_id=dock-static' y)
click_at $((nx + 40)) $((ny + 10))
c2=$(cbs)
await 100 cbs_at_least $((c2 + 10)) || {
    echo "frame callbacks did not resume after the tab was reselected"; exit 1; }

expect_alive
echo "OK: a tabbed-behind window is suspended, the reselected tab resumes"
