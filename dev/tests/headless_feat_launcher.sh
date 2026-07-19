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
    [[ "$opened" -gt 2000 ]] && break
done
[[ "$opened" -gt 2000 ]] || { echo "launcher did not open ($opened)"; exit 1; }

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

pick_action() {
    local query="$1"

    ctl "key 125 press"
    ctl "key 60 press"
    ctl "key 60 release"
    ctl "key 125 release"
    sleep 0.2
    ctl "type $query"
    sleep 0.3
    ctl "key 108 press"; ctl "key 108 release" # Down: select the filtered action
    ctl "key 28 press"; ctl "key 28 release"   # Enter
    sleep 0.3
}

pick_action settings
screenshot "$XDG_RUNTIME_DIR/settings.ppm"
settings_diff=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/settings.ppm" 0 30 1280 780)
[[ "$settings_diff" -gt 2000 ]] || { echo "launcher did not open settings ($settings_diff)"; exit 1; }

# The action is a toggle, like inspector: selecting it again closes the dialog.
pick_action settings
screenshot "$XDG_RUNTIME_DIR/settings-closed.ppm"
settings_closed=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/settings-closed.ppm" 0 30 1280 780)
[[ "$settings_closed" -lt 1000 ]] || { echo "settings did not close ($settings_closed)"; exit 1; }

pick_action "lock screen"
screenshot "$XDG_RUNTIME_DIR/locked.ppm"
locked=$(region_diff "$XDG_RUNTIME_DIR/base.ppm" "$XDG_RUNTIME_DIR/locked.ppm" 0 30 1280 780)
# The procedural desktop is deliberately close to the lockscreen's dark
# tint.  Assert the visible dialog and the actual security boundary instead
# of coupling this test to shadows/flat-background pixel counts.
[[ "$locked" -gt 1000 ]] || { echo "launcher did not show lock dialog ($locked)"; exit 1; }
[[ "$(dump_field '^captured ' kb)" = 1 && "$(dump_field '^captured ' ptr)" = 1 ]] || {
    echo "launcher lock screen did not capture input"
    exit 1
}

ctl "type xxx"
sleep 0.4
ctl "key 28 press"; ctl "key 28 release"
await 50 in_log "lockscreen closed" || { echo "launcher lockscreen did not unlock"; exit 1; }

echo "OK: launcher opens settings and lock screen actions"
