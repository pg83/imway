#!/usr/bin/env bash
# The dock's cycle click among other applications' slots: a pinned
# application with one window (its slot first) and a window without an
# app_id sit beside a two-window group (with a third window of the group
# that never maps). A click on the lone window's slot focuses it, clicks on
# the group's slot alternate its two mapped windows and never reach the
# other applications' windows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set desktop.active_click 2"
ctl "set desktop.mru_order false" # slots keep their places while the focus moves
ctl "set desktop.pinned_apps dock-one"
await 100 in_log "control: set desktop.pinned_apps" || { echo "the dock settings were not taken"; exit 1; }

start_client
wait_client "mapped"
four() { [[ "$(dump_state | grep -c '^toplevel .*mapped=1')" -eq 4 ]]; }
await 100 four || { echo "the four windows did not map"; dump_state; exit 1; }

id_of() { # <dump pattern>
    dump_field "$1" id
}
one=$(id_of 'app_id=dock-one')
read -r twoA twoB < <(dump_state | awk '$1 == "toplevel" && / mapped=1 / && / app_id=dock-two / { for (i = 1; i <= NF; i++) if ($i ~ /^id=/) print substr($i, 4) }' | xargs)
[[ -n "$one" && -n "$twoA" && -n "$twoB" ]] || { echo "the windows are not all in the scene"; dump_state; exit 1; }
focus_id() { dump_field '^focus ' id; }
focused_is() { [[ "$(focus_id)" == "$1" ]]; }

# slots one dock slot apart: the pinned application first, then the group
# and the unnamed window in the order they mapped
slot=$(dump_field '^imgui name=##dock ' w)
click_slot() { # <index> <want focus id...>: click until one of them has the focus
    local i
    shift_want=("${@:2}")
    has_want() { local w; for w in "${shift_want[@]}"; do focused_is "$w" && return 0; done; return 1; }
    for i in 1 2 3; do
        click_at 29 $((29 + $1 * (slot - 5)))
        await 30 has_want && return 0
    done
    return 1
}

click_slot 0 "$one" || { echo "the lone window's slot did not focus it (focus $(focus_id))"; dump_state; exit 1; }

click_slot 1 "$twoA" "$twoB" || { echo "the group's slot did not focus one of its windows (focus $(focus_id))"; dump_state; exit 1; }
first=$(focus_id)
other=$twoA
[[ "$first" == "$twoA" ]] && other=$twoB
click_slot 1 "$other" || { echo "the group's slot did not cycle to its other window (focus $(focus_id))"; dump_state; exit 1; }
click_slot 1 "$first" || { echo "the group's slot did not cycle back (focus $(focus_id))"; dump_state; exit 1; }

touch "$XDG_RUNTIME_DIR/done-go"
wait_client "done"
expect_client_ok "the client failed"
expect_alive "compositor died cycling dock slots"
echo "OK: the cycle click focuses a lone window and alternates a group's windows only"
