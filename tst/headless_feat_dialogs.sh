#!/usr/bin/env bash
# Every compositor-owned dialog opens and closes: the log view and the
# inspector from their launcher actions and shortcuts, the notification
# history from its action, the calendar from the bar clock. The state dump
# names the ImGui windows, so each one is asserted by name.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# a short clock makes the bar's right edge predictable
ctl "set desktop.clock_date false"
ctl "set desktop.clock_locale false"
ctl "set desktop.clock_24_hour true"
ctl "set desktop.clock_seconds false"
ctl "set desktop.wifi_indicator false"
ctl "set desktop.layout_indicator false"
await 20 in_log "control: set desktop.layout_indicator" || { echo "settings are not reachable"; exit 1; }
sleep 0.3

window() { # <imgui window name prefix>
    [[ -n "$(dump_field "^imgui name=$1" x)" ]]
}
no_window() {
    [[ -z "$(dump_field "^imgui name=$1" x)" ]]
}

# Each step waits for its own result. The launcher has to be taking text
# before the query is typed, or ImGui stops trickling the characters ahead
# of the Up queued behind them and the unfiltered grid gets navigated; and
# the launcher is still drawn for the frame in which the action it fired
# has already toggled a dialog, so the toggle is only judged once it is off
# the screen.
action() { # <launcher action name>
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "the launcher did not open for '$1'"; exit 1; }
    ctl "type $1"
    ctl "key 103 press"; ctl "key 103 release" # Up: select the action row
    ctl "key 28 press"; ctl "key 28 release"
    await_no_imgui '##launcher' || { echo "the launcher did not close after '$1'"; exit 1; }
}

# the log view: its own action, and the action again closes it
action log
await 50 window 'log ' || { echo "the log view did not open"; dump_state; exit 1; }
action log
await 50 no_window 'log ' || { echo "the log action did not toggle the view off"; dump_state; exit 1; }

# the inspector: from the shortcut and from the action
ctl "key 125 press"; ctl "key 88 press"; ctl "key 88 release"; ctl "key 125 release" # Super+F12
await 50 window 'inspector' || { echo "Super+F12 did not open the inspector"; dump_state; exit 1; }
sleep 1.2 # let it sample the frame-time ring at least once
ctl "key 125 press"; ctl "key 88 press"; ctl "key 88 release"; ctl "key 125 release"
await 50 no_window 'inspector' || { echo "Super+F12 did not close the inspector"; exit 1; }
action inspector
await 50 window 'inspector' || { echo "the inspector action did not open it"; dump_state; exit 1; }
action inspector
await 50 no_window 'inspector' || { echo "the inspector action did not close it"; exit 1; }

# the notification history
action notifications
await 50 window '##history' || { echo "the history panel did not open"; dump_state; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await 50 no_window '##history' || { echo "Escape did not close the history panel"; exit 1; }

# the calendar from the clock at the right end of the bar
bar_w=$(dump_field '^imgui name=##MainMenuBar' w)
bar_x=$(dump_field '^imgui name=##MainMenuBar' x)
click_at $((bar_x + bar_w - 25)) 11
await 50 window '##calendar' || { echo "the clock did not open the calendar"; dump_state; exit 1; }

# its month arrows and the today button, then Escape
cx=$(dump_field '^imgui name=##calendar' x); cy=$(dump_field '^imgui name=##calendar' y); cw=$(dump_field '^imgui name=##calendar' w)
# twenty pixels of a redrawn month label: take fresh frames until the
# header changes rather than judging whichever one a fixed sleep lands on
month_stepped() {
    screenshot "$XDG_RUNTIME_DIR/month-back.ppm" &&
        [[ "$(region_diff "$XDG_RUNTIME_DIR/month0.ppm" "$XDG_RUNTIME_DIR/month-back.ppm" \
            "$cx" "$cy" $((cx + cw)) $((cy + 60)))" -gt 20 ]]
}

screenshot "$XDG_RUNTIME_DIR/month0.ppm"
click_at $((cx + 14)) $((cy + 14))
await 50 month_stepped || { echo "the calendar did not step back a month"; exit 1; }
click_at $((cx + cw - 14)) $((cy + 14))
sleep 0.3
ctl "key 1 press"; ctl "key 1 release"
await 50 no_window '##calendar' || { echo "Escape did not close the calendar"; exit 1; }

expect_alive "compositor died opening its dialogs"
echo "OK: the log view, inspector, history and calendar all open and close"
