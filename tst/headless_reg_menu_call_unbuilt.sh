#!/usr/bin/env bash
# private-session-bus
# imway-env: IMWAY_CHAOS="dbus-message=AboutToShow dbus-message=Event"
# A menu call libdbus cannot build is dropped whole: the first AboutToShow
# for the empty submenu and the first Event for the Help leaf fail to
# allocate, the application hears neither, and the next open of the
# heading and the next click on the leaf each send theirs once.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_global_menu"
start_client
wait_client "global menu mapped"
wait_client "layout revision 1"
wait_mapped

heading() { # <label> <field>
    dump_state | awk -v l="label=$1" -v f="$2" '$1 == "menubar" && $NF == l { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }'
}
has_heading() { [[ -n "$(heading "$1" x0)" ]]; }
popups() { dump_state | grep -c '^imgui name=##\(Popup\|Menu\)' || true; }
popup_up() { (( $(popups) >= 1 )); }
popup_gone() { (( $(popups) == 0 )); }
heard() { grep -c "^$1$" "$CLIENT_LOG" || true; }

click_heading() { # <label>
    click_at $(( ($(heading "$1" x0) + $(heading "$1" x1)) / 2 )) $(( ($(heading "$1" y0) + $(heading "$1" y1)) / 2 ))
}
# a click that landed before the frame putting the heading under the
# pointer is repeated
open_heading() { # <label>
    local i
    for i in 1 2 3; do
        click_heading "$1"
        await 30 popup_up && return 0
    done
    echo "the $1 heading did not open its menu"
    dump_state
    exit 1
}
close_menu() { # <label>
    click_heading "$1"
    await 50 popup_gone || { echo "a click on the open $1 heading did not close its menu"; dump_state; exit 1; }
}

await 100 has_heading Empty || { echo "the bar reports no Empty heading"; dump_state; exit 1; }

# the first open asks for nothing the application hears; the menu still
# shows, waiting for its children
open_heading Empty
close_menu Empty
open_heading Empty
await 50 grep -q "^about 3$" "$CLIENT_LOG" || { echo "the second open of the empty submenu was not prepared"; cat "$CLIENT_LOG"; exit 1; }
(( $(heard "about 3") == 1 )) || { echo "the unbuilt AboutToShow reached the application"; cat "$CLIENT_LOG"; exit 1; }
close_menu Empty

# Help is a leaf on the bar: a click activates it, until the application
# has heard one activation
help=0
for _ in 1 2 3 4; do
    click_heading Help
    if await 30 grep -q "^event 2$" "$CLIENT_LOG"; then
        help=1
        break
    fi
done
(( help )) || { echo "no click on Help ever activated it"; cat "$CLIENT_LOG"; exit 1; }
(( $(heard "event 2") == 1 )) || { echo "the unbuilt Event reached the application"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died on a menu call it could not build"
echo "OK: an unbuilt menu call is dropped and the next one goes out"
