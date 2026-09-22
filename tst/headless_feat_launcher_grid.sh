#!/usr/bin/env bash
# The launcher's grid: arrow keys walk the applications and system actions
# spatially (up into a row above, across from the system group into the
# applications' last row, down back out to the input line), Enter runs the
# highlighted entry, and a click on a cell runs that one. Entries the desktop
# spec hides -- NoDisplay, Hidden, no Name, no Exec, not an Application, an
# empty file, a non-.desktop file or directory -- must not take a cell: they
# are named to sort between the real ones, so any of them showing up would
# move every pick after it. The host's own data dirs stay out of the search, so
# its desktop entries do too; the Vulkan loader finds its drivers through
# them, which vk/ lends it alone.
# imway-env: XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./vk
# imway-pre: mkdir -p vk && ln -s /usr/share/vulkan vk/vulkan
# imway-pre: mkdir -p xdg/applications/sub.desktop
set -euo pipefail
. "$(dirname "$0")/lib.sh"

apps=xdg/applications
for i in $(seq -w 1 13); do
    printf '[Desktop Entry]\nType=Application\nName=app%s\nExec=sh -c "echo app%s > picked.out" %%U\n' "$i" "$i" >"$apps/app$i.desktop"
done
# the last line without its newline
printf '[Desktop Entry]\nType=Application\nName=app14\nExec=sh -c "echo app14 > picked.out"' >"$apps/app14.desktop"
printf '[Desktop Entry]\nType=Application\nName=app01a\nExec=true\nNoDisplay=true\n' >"$apps/hidden1.desktop"
printf '[Desktop Entry]\nType=Application\nName=app02a\nExec=true\nHidden=true\n' >"$apps/hidden2.desktop"
printf '[Desktop Entry]\nType=Link\nName=app03a\nExec=true\n' >"$apps/link.desktop"
printf '[Desktop Entry]\nType=Application\nExec=true\n' >"$apps/noname.desktop"
printf '[Desktop Entry]\nType=Application\nName=app04a\n' >"$apps/noexec.desktop"
: >"$apps/empty.desktop"
printf '[Desktop Entry]\nType=Application\nName=app05a\nExec=true\n' >"$apps/README"
napps=14

open_launcher() {
    rm -f picked.out
    launcher_up=1
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
}

# the column count from the window's width: padding on both sides, then
# cells of the dock's icon size with the slot's breathing room between them,
# one dock slot per column (the dock's own width)
open_launcher
lw=$(dump_field '^imgui name=##launcher ' w)
lx=$(dump_field '^imgui name=##launcher ' x); ly=$(dump_field '^imgui name=##launcher ' y)
slot=$(dump_field '^imgui name=##dock ' w)
grid=$((lw - 16 + 10))
(( grid % slot == 0 )) || grid=$((grid - 14)) # a scrollbar beside the grid
(( grid % slot == 0 )) || { echo "a ${lw}px launcher is no whole number of ${slot}px slots"; exit 1; }
cols=$((grid / slot))
echo "launcher ${lw}px wide at $lx,$ly: $cols columns"

# the compositor's walk, replayed: sel 0 is the input line, 1.. the cells,
# applications first, then the six system actions
expect_pick() { # <cols> <keys...> -> the name the walk lands on
    python3 - "$napps" "$@" <<'PY'
import sys
apps, cols, keys = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3:]
sysn = 6
names = ['app%02d' % (i + 1) for i in range(apps)] + sorted(
    ['lock screen', 'settings', 'notifications', 'inspector', 'color picker', 'log'])
n = apps + sysn
sel = 0
for k in keys:
    if k == 'up' and n:
        if sel == 0:
            cnt = sysn if sysn else apps
            base = apps if sysn else 0
            sel = base + ((cnt - 1) // cols) * cols + 1
        else:
            i = sel - 1
            ins = i >= apps
            gi = i - apps if ins else i
            r, col = gi // cols, gi % cols
            if r > 0:
                cnt = sysn if ins else apps
                t = (r - 1) * cols + col
                sel = (apps if ins else 0) + (t if t < cnt else cnt - 1) + 1
            elif ins and apps:
                t = ((apps - 1) // cols) * cols + col
                sel = (t if t < apps else apps - 1) + 1
    elif k == 'down' and sel > 0:
        i = sel - 1
        ins = i >= apps
        gi = i - apps if ins else i
        r, col = gi // cols, gi % cols
        cnt = sysn if ins else apps
        if r < (cnt - 1) // cols:
            t = (r + 1) * cols + col
            sel = (apps if ins else 0) + (t if t < cnt else cnt - 1) + 1
        elif not ins and sysn:
            sel = apps + (col if col < sysn else sysn - 1) + 1
        else:
            sel = 0
print(names[sel - 1] if sel else '-')
PY
}

walk() { # <keys...>: open, walk, Enter; the landed application must run
    local want k
    want=$(expect_pick "$cols" "$@")
    [[ "$want" == app* ]] || { echo "walk $* lands on '$want', not an application"; exit 1; }
    ((launcher_up)) || open_launcher
    for k in "$@"; do
        case $k in
            up) ctl "key 103 press"; ctl "key 103 release" ;;
            down) ctl "key 108 press"; ctl "key 108 release" ;;
        esac
    done
    ctl "key 28 press"; ctl "key 28 release"
    await 100 test -s picked.out || { echo "walk $* launched nothing (expected $want)"; exit 1; }
    [[ "$(cat picked.out)" == "$want" ]] || { echo "walk $* ran $(cat picked.out), expected $want"; exit 1; }
    echo "walk $*: $want"
    launcher_up=0
    await_no_imgui '##launcher' || { echo "launcher stayed open after a pick"; exit 1; }
    rm -f picked.out
}

walk up up
walk up up up
walk up up up up up up up down
walk up up down down up up
walk up up up down down up up up

# a click on the top-left cell runs the first application
open_launcher
click_at $((lx + 8 + 24)) $((ly + 8 + 24))
await 100 test -s picked.out || { echo "a click on a cell launched nothing"; exit 1; }
[[ "$(cat picked.out)" == app01 ]] || { echo "the top-left cell ran $(cat picked.out)"; exit 1; }

expect_alive "compositor died walking the launcher grid"
echo "OK: the launcher grid walks spatially, runs the highlighted and the clicked entry, and hides what the spec hides"
