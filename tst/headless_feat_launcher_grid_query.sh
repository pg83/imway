#!/usr/bin/env bash
# The launcher grid's arrow walk at its edges, with a query thinning the
# groups out: Down on the input line stays there, Down from the last
# application row with no system action shown goes back to the input line,
# Up from the top of the system group with no application shown stays put,
# and on a portrait output the system actions wrap into more than one row,
# which Up and Down walk inside the group. Enter runs what the walk landed
# on: the application, or the settings action (a toggle).
# imway-args: --mode 800x1280
# imway-env: IMWAY_FAKE_KMS_MODE=800x1280 XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./vk
# imway-pre: mkdir -p vk && ln -s /usr/share/vulkan vk/vulkan
# imway-pre: mkdir -p xdg/applications
set -euo pipefail
. "$(dirname "$0")/lib.sh"

printf '[Desktop Entry]\nType=Application\nName=app1\nExec=sh -c "echo app1 > picked.out"\n' >xdg/applications/app1.desktop

open_launcher() { # [query]
    rm -f picked.out
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
    if [[ -n "${1:-}" ]]; then
        ctl "type $1"
        await_input "$1" || { echo "the query '$1' did not land in the launcher"; exit 1; }
    fi
}

# the column count from the window's width, as the grid scenario reads it
open_launcher
lw=$(dump_field '^imgui name=##launcher ' w)
slot=$(dump_field '^imgui name=##dock ' w)
grid=$((lw - 16 + 10))
(( grid % slot == 0 )) || grid=$((grid - 14)) # a scrollbar beside the grid
(( grid % slot == 0 )) || { echo "a ${lw}px launcher is no whole number of ${slot}px slots"; exit 1; }
cols=$((grid / slot))
echo "launcher ${lw}px wide: $cols columns"
(( cols < 6 )) || { echo "the six system actions fit one row of $cols: the in-group walk is not exercised"; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await_no_imgui '##launcher' || { echo "Escape did not close the launcher"; exit 1; }

# the compositor's walk, replayed over the rows the query leaves: sel 0 is
# the input line, 1.. the cells, applications first, then system actions
expect_pick() { # <query> <keys...> -> the name the walk lands on
    python3 - "$cols" "$@" <<'PY'
import sys
cols, query, keys = int(sys.argv[1]), sys.argv[2].lower(), sys.argv[3:]
apps = [a for a in ['app1'] if query in a]
system = [s for s in sorted(['lock screen', 'settings', 'notifications', 'inspector', 'color picker', 'log']) if query in s]
names = apps + system
na, ns = len(apps), len(system)
sel = 0
for k in keys:
    if k == 'up' and names:
        if sel == 0:
            cnt = ns if ns else na
            base = na if ns else 0
            sel = base + ((cnt - 1) // cols) * cols + 1
        else:
            i = sel - 1
            ins = i >= na
            gi = i - na if ins else i
            r, col = gi // cols, gi % cols
            if r > 0:
                sel = (na if ins else 0) + (r - 1) * cols + col + 1
            elif ins and na:
                t = ((na - 1) // cols) * cols + col
                sel = min(t, na - 1) + 1
    elif k == 'down' and sel > 0:
        i = sel - 1
        ins = i >= na
        gi = i - na if ins else i
        r, col = gi // cols, gi % cols
        cnt = ns if ins else na
        if r < (cnt - 1) // cols:
            sel = (na if ins else 0) + min((r + 1) * cols + col, cnt - 1) + 1
        elif not ins and ns:
            sel = na + min(col, ns - 1) + 1
        else:
            sel = 0
print(names[sel - 1] if sel else '-')
PY
}

walk() { # <query> <keys...>: open, type, walk, Enter; the landed entry must run
    local query=$1 want k
    shift
    want=$(expect_pick "$query" "$@")
    open_launcher "$query"
    for k in "$@"; do
        case $k in
            up) ctl "key 103 press"; ctl "key 103 release" ;;
            down) ctl "key 108 press"; ctl "key 108 release" ;;
        esac
    done
    ctl "key 28 press"; ctl "key 28 release"
    case $want in
        app1)
            await 100 test -s picked.out || { echo "walk '$query' $* launched nothing (expected $want)"; exit 1; }
            [[ "$(cat picked.out)" == "$want" ]] || { echo "walk '$query' $* ran $(cat picked.out), expected $want"; exit 1; }
            ;;
        settings)
            # the action toggles: the second settings walk closes it again
            if ((settings_open)); then
                await_no_imgui settings || { echo "walk '$query' $* did not close settings"; exit 1; }
                settings_open=0
            else
                await_imgui settings || { echo "walk '$query' $* did not open settings"; exit 1; }
                settings_open=1
            fi
            ;;
        *)
            echo "walk '$query' $* lands on '$want', which this scenario cannot tell apart"
            exit 1
            ;;
    esac
    echo "walk '$query' $*: $want"
    await_no_imgui '##launcher' || { echo "launcher stayed open after a pick"; exit 1; }
    rm -f picked.out
}

settings_open=0
walk "" down up up up          # Down on the input line does nothing
walk app up down up            # no system row below: Down leaves the grid
walk settings up up            # no application row above: Up stays
walk s up up down              # a system row above and back down

expect_alive "compositor died walking the launcher grid's edges"
echo "OK: the launcher walk keeps to its edges with a query and walks inside a wrapped system group"
