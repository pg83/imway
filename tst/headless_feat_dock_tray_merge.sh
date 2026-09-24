#!/usr/bin/env bash
# private-session-bus
# A tray item whose id names a running application's window shares that
# window's dock slot instead of taking its own: a Passive item alone gets no
# slot, but once the window maps the slot shows the item's icon and its menu
# holds the window's items and then, past a separator, the item's own. An
# item with neither an id nor a title gets a slot of its own whose tooltip
# still says something. Unmerging the tray and hiding it work as they say.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

sni="$IMWAY_TESTS_BIN/client_feat_dock_status_notifier"

# the magenta tray pixmaps in the dock column, one "top bottom" line per slot
slots() {
    screenshot "$XDG_RUNTIME_DIR/_slots.ppm"
    python3 - "$XDG_RUNTIME_DIR/_slots.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); px = f.read(w * h * 3)
rows = [y for y in range(h) if any(px[(y * w + x) * 3:(y * w + x) * 3 + 3] == b'\xff\x00\xff' for x in range(58))]
runs = []
for y in rows:
    if runs and y == runs[-1][1] + 1:
        runs[-1][1] = y
    else:
        runs.append([y, y])
for a, b in runs:
    print(a, b)
PY
}
slot_count() { slots | wc -l; }

SNI_ID=dock-test SNI_STATUS=Passive SNI_SERVICE=org.example.ImwayTrayA "$sni" >"$XDG_RUNTIME_DIR/a.log" 2>&1 &
await 100 grep -q "layout requested" "$XDG_RUNTIME_DIR/a.log" || { echo "the passive item did not register"; cat "$XDG_RUNTIME_DIR/a.log"; exit 1; }
await 50 eval 'dump_state | grep -q "^tray id=dock-test status=Passive"' || { echo "the passive item is not known"; exit 1; }
[[ "$(slot_count)" -eq 0 ]] || { echo "a passive item alone took a dock slot: $(slots | xargs)"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_dock"
start_client
wait_mapped
one() { [[ "$(slot_count)" -eq 1 ]]; }
await 50 one || { echo "the window's slot does not show the item's icon: $(slots | xargs)"; exit 1; }

SNI_ID= SNI_TITLE= SNI_SERVICE=org.example.ImwayTrayB "$sni" >"$XDG_RUNTIME_DIR/b.log" 2>&1 &
two() { [[ "$(slot_count)" -eq 2 ]]; }
await 100 two || { echo "the nameless item did not get a slot of its own: $(slots | xargs)"; exit 1; }
read -r a0 a1 b0 b1 < <(slots | xargs)

# the tooltip over each slot: the window's title "dock-test", then the
# nameless item's stand-in "application", which is the wider of the two
tip_w() { dump_field '^imgui name=##Tooltip_' w; }
hover() { # <y>
    ctl "motion 500 500"
    await 50 eval '[[ -z "$(tip_w)" ]]' || { echo "a tooltip stayed up"; exit 1; }
    ctl "motion 29 $1"
    await 50 eval '[[ -n "$(tip_w)" ]]' || { echo "no tooltip over the slot at y=$1"; exit 1; }
    tip_w
}
named=$(hover $(((a0 + a1) / 2)))
nameless=$(hover $(((b0 + b1) / 2)))
(( nameless > named )) || { echo "the nameless slot's tooltip ($nameless) is not wider than 'dock-test' ($named)"; exit 1; }

# the merged slot's menu: the window's five rows, a separator, the item's
# action last
ctl "motion 29 $(((a0 + a1) / 2))"
ctl "button right press"; ctl "button right release"
popup() { dump_field '^imgui name=##Popup_' "$1"; }
await 50 eval '[[ -n "$(popup h)" ]]' || { echo "the merged slot's menu did not open"; dump_state; exit 1; }
await 50 grep -q "layout requested" "$XDG_RUNTIME_DIR/a.log"
click_at $(($(popup x) + $(popup w) / 2)) $(($(popup y) + $(popup h) - 12))
await 50 grep -q "menu clicked" "$XDG_RUNTIME_DIR/a.log" || { echo "the last row is not the item's action"; cat "$XDG_RUNTIME_DIR/a.log"; exit 1; }

# unmerged, the passive item leaves the window's slot and, being passive,
# takes none of its own: the nameless item's is the only tray icon left
ctl "motion 500 500"
ctl "set desktop.merge_tray false"
await 50 one || { echo "unmerged, the passive item still shows in the dock: $(slots | xargs)"; exit 1; }
# the tray hidden, no item shows at all
ctl "set desktop.show_tray false"
none() { [[ "$(slot_count)" -eq 0 ]]; }
await 50 none || { echo "the hidden tray still shows in the dock: $(slots | xargs)"; exit 1; }
ctl "set desktop.show_tray true"
await 50 one || { echo "the tray shown again lost its item: $(slots | xargs)"; exit 1; }

expect_alive "compositor died merging tray items into the dock"
echo "OK: tray items merge into their window's slot, passive ones take none; the settings unmerge and hide them"
