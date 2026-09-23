#!/usr/bin/env bash
# The switcher's less travelled paths: driven from the right Alt, a second
# Tab steps on from the selection rather than from the focused window, so two
# steps over two windows come back round; the Tab let go after its selection
# was destroyed commits nothing; and with only a window that never mapped
# left, Alt+Tab walks the whole ring, finds nothing and stays out of the way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_alt_tab_cycle"
start_client
wait_client "alt-tab cycle: windows up"
wait_rect 'app_id=alt-tab-plain'
wait_rect 'app_id=alt-tab-wide'

focused_app() {
    dump_state | awk '$1 == "toplevel" && / focused=1 / { for (i = 1; i <= NF; i++) if ($i ~ /^app_id=/) print substr($i, 8) }'
}
captured() { dump_field '^captured ' kb; }
switcher_up() { [[ "$(captured)" == 1 ]]; }
switcher_down() { [[ "$(captured)" == 0 ]]; }
have_focus() { [[ -n "$(focused_app)" ]]; }
await 50 have_focus || { echo "no window got the focus"; dump_state; exit 1; }

# right Alt, Tab twice: round the two mapped windows back to the start
before=$(focused_app)
ctl "key 100 press"
ctl "key 15 press"
await 50 switcher_up || { echo "the switcher did not come up on the right Alt"; dump_state; exit 1; }
ctl "key 15 press"; ctl "key 15 release"
screenshot "$XDG_RUNTIME_DIR/_switcher.ppm"
ctl "key 100 release"
await 50 switcher_down || { echo "releasing the right Alt did not end the switch"; dump_state; exit 1; }
sleep 0.3
[[ "$(focused_app)" == "$before" ]] || { echo "two steps over two windows did not come back to $before (now $(focused_app))"; exit 1; }

# select the other window, destroy it with Tab still down, then let go of
# Tab: nothing to commit, the focus stays where it was
if [[ "$before" != alt-tab-wide ]]; then
    ctl "key 56 press"; ctl "key 15 press"; ctl "key 15 release"
    await 50 switcher_up || { echo "the switcher did not come up"; exit 1; }
    ctl "key 56 release"
    await 50 eval '[[ "$(focused_app)" == alt-tab-wide ]]' || { echo "could not focus the wide window"; dump_state; exit 1; }
fi
ctl "key 56 press"
ctl "key 15 press"
await 50 switcher_up || { echo "the switcher did not come up"; exit 1; }
kill -USR1 "$CLIENT_PID"
wait_client "plain window dropped"
gone() { ! dump_state | grep -q 'app_id=alt-tab-plain'; }
await 50 gone || { echo "the plain window did not go away"; exit 1; }
ctl "key 15 release"
ctl "key 56 release"
await 50 switcher_down || { echo "the switch did not end"; dump_state; exit 1; }
[[ "$(focused_app)" == alt-tab-wide ]] || { echo "the focus moved after the selection vanished"; exit 1; }

# only the never-mapped window left
kill -USR2 "$CLIENT_PID"
wait_client "wide window dropped"
only_pending() { [[ "$(dump_state | grep -c '^toplevel ')" -eq 1 ]] && dump_state | grep -q '^toplevel .*app_id=alt-tab-pending'; }
await 50 only_pending || { echo "the wide window did not go away"; dump_state; exit 1; }
ctl "key 56 press"
ctl "key 15 press"; ctl "key 15 release"
screenshot "$XDG_RUNTIME_DIR/_none.ppm"
switcher_down || { echo "Alt+Tab over an unmapped window took the keyboard"; exit 1; }
ctl "key 56 release"

expect_alive "compositor died on the switcher's edges"
echo "OK: the switcher steps from its selection, commits nothing gone, skips the unmapped"
