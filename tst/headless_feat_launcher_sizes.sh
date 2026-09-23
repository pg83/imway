#!/usr/bin/env bash
# The launcher at the ends of its range, rescanned on every open. With no
# desktop entries at all it holds only the system group, which Up and Enter
# still pick from. A query that matches nothing leaves no grid to walk, and
# Enter runs the text as a command. With more entries than fit in the
# window's height the grid scrolls, the window one scrollbar wider.
# imway-env: XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./vk
# imway-pre: mkdir -p vk xdg/applications && ln -s /usr/share/vulkan vk/vulkan
set -euo pipefail
. "$(dirname "$0")/lib.sh"

open_launcher() {
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "launcher did not open"; dump_state; exit 1; }
}
key() { ctl "key $1 press"; ctl "key $1 release"; }

# no entries: Up enters the system group from below, at the first cell of
# its last row; which action that is follows from the column count, one
# dock slot per column
open_launcher
lw_empty=$(dump_field '^imgui name=##launcher ' w)
slot=$(dump_field '^imgui name=##dock ' w)
cols=$(((lw_empty - 16 + 10) / slot))
actions=("color picker" "inspector" "lock screen" "log" "notifications" "settings")
want=${actions[$(((5 / cols) * cols))]}
echo "no entries: ${lw_empty}px, $cols columns, Up lands on $want"
key 103 # Up
key 28  # Enter
await_no_imgui '##launcher' || { echo "Enter in an application-less launcher did not act"; exit 1; }
window_up() { dump_state | grep -q "^imgui name=$1 "; }
case $want in
    "color picker")
        # armed, the next click reads a pixel and posts it
        click_at 700 400
        picked() { [[ "$(dump_field '^notifications ' history)" -ge 1 ]]; }
        await 50 picked || { echo "the color picker did not arm"; exit 1; } ;;
    inspector) await 50 window_up inspector || { echo "the inspector did not open"; exit 1; } ;;
    log) await 50 window_up log || { echo "the log did not open"; exit 1; } ;;
    notifications) await 50 window_up '##history' || { echo "the history did not open"; exit 1; } ;;
    settings) await 50 window_up settings || { echo "settings did not open"; exit 1; } ;;
esac

# nothing matches: Up has nothing to walk, Enter runs the query
open_launcher
lx=$(dump_field '^imgui name=##launcher ' x); ly=$(dump_field '^imgui name=##launcher ' y)
lw=$(dump_field '^imgui name=##launcher ' w); lh=$(dump_field '^imgui name=##launcher ' h)
screenshot "$XDG_RUNTIME_DIR/full.ppm"
ctl "type touch nomatch.out"
# the text trickles in a character a frame (the window keeps its shape):
# wait until the grid has emptied and the field stopped changing
settled_empty() {
    screenshot "$XDG_RUNTIME_DIR/a.ppm" && sleep 0.3 && screenshot "$XDG_RUNTIME_DIR/b.ppm" &&
        (( $(region_diff "$XDG_RUNTIME_DIR/a.ppm" "$XDG_RUNTIME_DIR/b.ppm" "$lx" "$ly" $((lx + lw)) $((ly + lh))) == 0 )) &&
        (( $(region_diff "$XDG_RUNTIME_DIR/full.ppm" "$XDG_RUNTIME_DIR/b.ppm" "$lx" "$ly" $((lx + lw)) $((ly + lh - 40))) > 200 ))
}
await 50 settled_empty || { echo "the query did not settle into an empty grid"; exit 1; }
key 103 # Up
key 28  # Enter
await 100 test -e nomatch.out || { echo "a query that matched nothing did not run as a command"; exit 1; }

# many entries: more rows than fit
for i in $(seq -w 1 150); do
    printf '[Desktop Entry]\nType=Application\nName=bulk%s\nExec=true\n' "$i" >"xdg/applications/bulk$i.desktop"
done
open_launcher
lw_many=$(dump_field '^imgui name=##launcher ' w)
lh_many=$(dump_field '^imgui name=##launcher ' h)
echo "empty ${lw_empty}px wide, many ${lw_many}x${lh_many}"
(( lh_many <= 800 )) || { echo "the launcher grew past the screen: ${lh_many}px"; exit 1; }
# Up walks into the grid from below and keeps the selection in view: the
# window stays put while the grid scrolls inside it
key 103; key 103; key 103; key 103
[[ "$(dump_field '^imgui name=##launcher ' h)" == "$lh_many" ]] || { echo "walking the grid resized the launcher"; exit 1; }
key 1 # Escape
await_no_imgui '##launcher' || { echo "Escape did not close the launcher"; exit 1; }

expect_alive "compositor died at the launcher's size extremes"
echo "OK: no entries, no matches and more entries than fit"
