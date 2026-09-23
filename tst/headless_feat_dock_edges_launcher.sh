#!/usr/bin/env bash
# The dock's launcher slot on every edge opens the launcher next to the dock
# and wholly on screen: beside a right dock, under a top one, over a bottom
# one. With two windows ungrouped the horizontal docks lay two slots side by
# side before the launcher slot at their far end.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_screenshot_viewer"
start_client
wait_mapped
IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/client-b.log"
"$IMWAY_CLIENT" >"$IMWAY_CLIENT_LOG" 2>&1 &
two_windows() { [[ "$(dump_state | grep -c '^toplevel .*app_id=shot-source')" -eq 2 ]]; }
await 100 two_windows || { echo "the second window did not map"; dump_state; exit 1; }

set_setting() { # <key> <value>
    local seen
    seen=$(grep -c "control: set $1\$" "$IMWAY_LOG" || true)
    ctl "set $1 $2"
    taken() { (( $(grep -c "control: set $setting_key\$" "$IMWAY_LOG" || true) > setting_seen )); }
    setting_key=$1 setting_seen=$seen
    await 20 taken || { echo "settings are not reachable"; exit 1; }
}
set_setting desktop.group_windows false

win() { # <name> <field>
    dump_field "^imgui name=$1 " "$2"
}
launcher_up() { [[ -n "$(win '##launcher' x)" ]]; }
launcher_gone() { ! launcher_up; }

# <position ordinal> <what>: the dock on that edge, its launcher slot at its
# far end, the launcher it opens beside it and on screen
edge() {
    local dx dy dw dh sx sy lx ly lw lh i
    set_setting desktop.dock_position "$1"
    at_edge() {
        case $edge_pos in
            1) [[ "$(win '##dock' x)" -gt 600 ]] ;;
            2) [[ "$(win '##dock' w)" -gt 600 && "$(win '##dock' y)" -lt 100 ]] ;;
            3) [[ "$(win '##dock' w)" -gt 600 && "$(win '##dock' y)" -gt 600 ]] ;;
        esac
    }
    edge_pos=$1
    await 50 at_edge || { echo "the dock did not move to $2"; dump_state; exit 1; }
    dx=$(win '##dock' x); dy=$(win '##dock' y); dw=$(win '##dock' w); dh=$(win '##dock' h)
    if (( dw > dh )); then
        sx=$((dx + dw - 29)); sy=$((dy + dh / 2))
    else
        sx=$((dx + dw / 2)); sy=$((dy + dh - 29))
    fi
    for i in 1 2 3; do
        click_at "$sx" "$sy"
        await 30 launcher_up && break
    done
    launcher_up || { echo "the $2 dock's launcher slot did not open the launcher"; dump_state; exit 1; }
    lx=$(win '##launcher' x); ly=$(win '##launcher' y); lw=$(win '##launcher' w); lh=$(win '##launcher' h)
    echo "$2 dock $dx,$dy ${dw}x$dh, launcher $lx,$ly ${lw}x$lh"
    (( lx >= 0 && ly >= 0 && lx + lw <= 1280 && ly + lh <= 800 )) || { echo "the launcher of the $2 dock is off screen"; exit 1; }
    (( lx + lw <= dx || lx >= dx + dw || ly + lh <= dy || ly >= dy + dh )) || { echo "the launcher of the $2 dock covers the dock"; exit 1; }
    ctl "key 1 press"; ctl "key 1 release" # Escape
    await 50 launcher_gone || { echo "the launcher did not close"; exit 1; }
}

edge 1 right
edge 2 top
edge 3 bottom
set_setting desktop.dock_position 0

expect_alive "compositor died opening the launcher from the dock's edges"
echo "OK: the launcher opens beside the dock on the right, top and bottom edges"
