#!/usr/bin/env bash
# private-session-bus
# The global menu's less common items, from the conform client's layout: a
# heading whose children make it a menu without children-display (Edit)
# opens its popup, a heading that declares a submenu but has no children yet
# (Empty) asks the application to prepare it and shows "loading...", an
# invisible heading takes no place on the bar, and in File a row whose
# children make it a submenu (More) opens a nested menu next to the warning,
# unnamed and unchecked radio rows. Every heading is aimed at from the rect
# the bar reports in the state dump.
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
# a nested menu is a child window the dump does not list; opening it asks
# the application to prepare the row
nested_up() { grep -q "^about 29$" "$CLIENT_LOG"; }

await 100 has_heading Edit || { echo "the bar reports no Edit heading"; dump_state; exit 1; }
has_heading "Hidden heading" && { echo "an invisible heading was drawn on the bar"; exit 1; }

# click a heading's centre until its popup is up (a click that landed before
# the frame putting the heading under the pointer is repeated)
open_heading() { # <label>
    local i x y
    for i in 1 2 3; do
        x=$(( ($(heading "$1" x0) + $(heading "$1" x1)) / 2 ))
        y=$(( ($(heading "$1" y0) + $(heading "$1" y1)) / 2 ))
        click_at "$x" "$y"
        await 30 popup_up && return 0
    done
    echo "the $1 heading did not open its menu"
    dump_state
    exit 1
}
# a second click on the open heading closes its menu
close_menu() { # <label>
    local x y
    x=$(( ($(heading "$1" x0) + $(heading "$1" x1)) / 2 ))
    y=$(( ($(heading "$1" y0) + $(heading "$1" y1)) / 2 ))
    click_at "$x" "$y"
    await 50 popup_gone || { echo "a click on the open $1 heading did not close its menu"; dump_state; exit 1; }
}

open_heading Edit
close_menu Edit

open_heading Empty
await 50 grep -q "^about 3$" "$CLIENT_LOG" || { echo "the empty submenu was not prepared"; cat "$CLIENT_LOG"; exit 1; }
close_menu Empty

# File's last row is More: walk up from the popup's bottom edge until a
# hover opens the nested menu
open_heading File
px=$(dump_field '^imgui name=##Popup' x); py=$(dump_field '^imgui name=##Popup' y)
pw=$(dump_field '^imgui name=##Popup' w); ph=$(dump_field '^imgui name=##Popup' h)
warning_drawn() {
    screenshot "$XDG_RUNTIME_DIR/file.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/file.ppm" "$px" "$py" "$pw" "$ph" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
x0, y0, pw, ph = map(int, sys.argv[2:6])
orange = 0
for y in range(y0, y0 + ph):
    for x in range(x0, x0 + pw):
        r, g, b = d[(y*w+x)*3:(y*w+x)*3+3]
        if r > 200 and 130 < g < 210 and b < 110:
            orange += 1
sys.exit(0 if orange > 20 else 1)
PY
}
await 30 warning_drawn || { echo "the warning row is not drawn in its colour"; exit 1; }
nested=0
for dy in 10 14 18 22 26 30; do
    ctl "motion $((px + 40)) $((py + ph - dy))"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
    ctl "motion $((px + 41)) $((py + ph - dy))"
    if await 10 nested_up; then
        nested=1
        break
    fi
done
(( nested )) || { echo "hovering More did not open its nested menu"; dump_state; exit 1; }
close_menu File

expect_alive "compositor died drawing the global menu's odd items"
echo "OK: parent headings, an empty submenu, a hidden heading and File's odd rows"
