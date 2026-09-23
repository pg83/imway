#!/usr/bin/env bash
# private-session-bus
# The global menu's less common headings, from the conform client's layout:
# a heading whose children make it a menu without children-display (Edit)
# opens its popup, a heading that declares a submenu but has no children yet
# (Empty) asks the application to prepare it and shows "loading...", an
# invisible heading and a separator take no place on the bar, the informative
# Help heading is drawn blue, and a second click on an open
# heading closes its menu. Every heading is aimed at from the rect the bar
# reports in the state dump.
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
dump_state | grep -q '^menubar heading id=5 ' && { echo "a separator heading was drawn on the bar"; exit 1; }

# Help is informative: its label is drawn in the informative blue, not the
# grey of the other headings
blue_label() { # <label>
    screenshot "$XDG_RUNTIME_DIR/bar.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/bar.ppm" "$(heading "$1" x0)" "$(heading "$1" y0)" "$(heading "$1" x1)" "$(heading "$1" y1)" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w * h * 3)
x0, y0, x1, y1 = map(int, sys.argv[2:6])
blue = 0
for y in range(y0, y1):
    for x in range(x0, x1):
        r, g, b = d[(y * w + x) * 3:(y * w + x) * 3 + 3]
        if b > 180 and b > r + 60:
            blue += 1
sys.exit(0 if blue > 10 else 1)
PY
}
await 50 blue_label Help || { echo "the informative Help heading is not drawn blue"; exit 1; }
! blue_label Edit || { echo "the normal Edit heading is drawn blue"; exit 1; }

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

expect_alive "compositor died on the global menu's odd headings"
echo "OK: parent headings, an empty submenu and a hidden heading on the bar"
