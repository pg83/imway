#!/usr/bin/env bash
# Alt+Tab and Alt+Shift+Tab cycle between the mapped windows only: a
# toplevel that never got a buffer is neither offered nor focused, a wide
# window with a very long title takes its turn like any other. The switcher
# drops itself when the window it has selected goes away under it, and with
# no windows at all Alt+Tab has nothing to offer and stays out of the way.
# The switcher holds the keyboard while it is up, which the dump reports.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

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

# one full switch; <shift> for the backwards chord
switch() { # [shift]
    local before
    before=$(focused_app)
    ctl "key 56 press"
    [[ -n "${1:-}" ]] && ctl "key 42 press"
    ctl "key 15 press"; ctl "key 15 release"
    await 50 switcher_up || { echo "the switcher did not come up"; dump_state; exit 1; }
    screenshot "$XDG_RUNTIME_DIR/_switcher.ppm" # a frame with the overlay drawn
    [[ -n "${1:-}" ]] && ctl "key 42 release"
    ctl "key 56 release"
    await 50 switcher_down || { echo "releasing Alt did not end the switch"; exit 1; }
    moved() {
        local now
        now=$(focused_app)
        [[ -n "$now" && "$now" != "$before" ]]
    }
    await 50 moved || { echo "the switch from $before went nowhere (now $(focused_app))"; dump_state; exit 1; }
    [[ "$(focused_app)" != alt-tab-pending ]] || { echo "the switch focused a window that never mapped"; exit 1; }
}

switch
switch shift
switch
switch shift

# with the wide window focused, Tab selects the plain one; destroy it while
# Alt is still held and the switcher lets go
[[ "$(focused_app)" == alt-tab-wide ]] || switch
[[ "$(focused_app)" == alt-tab-wide ]] || { echo "could not focus the wide window"; exit 1; }
ctl "key 56 press"
ctl "key 15 press"; ctl "key 15 release"
await 50 switcher_up || { echo "the switcher did not come up"; exit 1; }
kill -USR1 "$CLIENT_PID"
wait_client "plain window dropped"
gone() { ! dump_state | grep -q 'app_id=alt-tab-plain'; }
await 50 gone || { echo "the plain window did not go away"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/_dropped.ppm" # a frame that saw the selection gone
# the keyboard hold is worked out per key event: tap Shift to have it asked
ctl "key 42 press"; ctl "key 42 release"
await 50 switcher_down || { echo "the switcher outlived its selected window"; dump_state; exit 1; }
ctl "key 56 release"
[[ "$(focused_app)" == alt-tab-wide ]] || { echo "the focus moved after the selection vanished"; exit 1; }

# no windows: Alt+Tab offers nothing and does not take the keyboard
kill "$CLIENT_PID"
no_toplevels() { ! dump_state | grep -q '^toplevel '; }
await 100 no_toplevels || { echo "the client's windows did not go away"; exit 1; }
ctl "key 56 press"
ctl "key 15 press"; ctl "key 15 release"
sleep 0.5
switcher_down || { echo "Alt+Tab with no windows took the keyboard"; exit 1; }
ctl "key 56 release"

expect_alive "compositor died cycling windows"
echo "OK: Alt+Tab and Alt+Shift+Tab cycle mapped windows and let go when there is nothing to hold"
