#!/usr/bin/env bash
# Launcher: Super+F2 opens it; a click outside dismisses it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

screenshot "$XDG_RUNTIME_DIR/base.ppm"
centroid "$XDG_RUNTIME_DIR/base.ppm" 255 255 255 >/dev/null 2>&1 || {
    echo "right-side bar widgets are missing"
    exit 1
}

ctl "key 125 press"  # Super
ctl "key 60 press"   # F2
ctl "key 60 release"
ctl "key 125 release"

opened=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/open.ppm"
    opened=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/open.ppm" 400 180 880 460)
    [[ "$opened" -gt 1500 ]] && break
done
[[ "$opened" -gt 1500 ]] || { echo "launcher did not open ($opened)"; exit 1; }

click_at 1000 650

closed=99999
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/closed.ppm"
    closed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/closed.ppm" 400 180 880 460)
    [[ "$closed" -lt 1000 ]] && break
done
echo "opened=$opened closed=$closed"
[[ "$closed" -lt 1000 ]] || { echo "launcher did not close ($closed)"; exit 1; }

# Open the launcher, type a query, run the first hit of the filtered grid.
# Every step waits for the compositor to show its result instead of sleeping
# a fixed amount: an instrumented build spends several frames on each of
# them, and what the next screenshot catches is then whatever was up.
pick_action() { # <query>
    local query="$1"

    ctl "key 125 press"
    ctl "key 60 press"
    ctl "key 60 release"
    ctl "key 125 release"
    await_typing '##launcher' || { echo "launcher did not open for '$query'"; exit 1; }

    ctl "type $query"
    ctl "key 103 press"; ctl "key 103 release" # Up: enter the grid from the input line
    ctl "key 28 press"; ctl "key 28 release"   # Enter
    # The action runs while the launcher is still being drawn, so for one
    # frame the dialog it toggled is already gone and the launcher is not.
    await_no_imgui '##launcher' || { echo "launcher did not close after '$query'"; exit 1; }
}

# Which dialog is up is the compositor's own answer, not a pixel count: the
# settings window covers procedural wallpaper, so a threshold over it says
# more about the rasterizer than about the action.
pick_action settings
await_imgui settings || { echo "launcher did not open settings"; exit 1; }

# The action is a toggle, like inspector: selecting it again closes the dialog.
pick_action settings
await_no_imgui settings || { echo "settings did not close"; exit 1; }

# The procedural desktop is deliberately close to the lockscreen's dark
# tint.  Assert the visible dialog and the actual security boundary instead
# of coupling this test to shadows/flat-background pixel counts.
pick_action "lock screen"
await_imgui '##lock-overlay' || { echo "launcher did not show the lock dialog"; exit 1; }
[[ "$(dump_field '^captured ' kb)" = 1 && "$(dump_field '^captured ' ptr)" = 1 ]] || {
    echo "launcher lock screen did not capture input"
    exit 1
}

ctl "type xxx"

await_input "xxx" || { echo "the field did not take 'xxx'"; dump_state; exit 1; }
ctl "key 28 press"; ctl "key 28 release"
await 50 in_log "lockscreen closed" || { echo "launcher lockscreen did not unlock"; exit 1; }

echo "OK: launcher opens settings and lock screen actions"
