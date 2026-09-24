#!/usr/bin/env bash
# The notifications settings page's application rules, driven by the
# pointer: a rule's policy combo, its name field and its remove button decide
# which of an application's notifications reach the screen, and "add
# application rule" makes a new one. Coordinates are relative to the settings
# window, whose page rows are one framed widget apart.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.timeout 60"
ctl "rule 0 2 muteme" # mute
await 100 in_log "control: rule 0" || { echo "the rule did not land"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>: click inside the settings window
    click_at $((wx + $1)) $((wy + $2))
}
at 40 $((38 + 7 * 20)) # the notifications page
ctl "motion 1000 700"

active() { dump_field '^notifications ' active; }
active_is() { [[ "$(active)" == "$1" ]]; }
# the page is a child window: it, not the dialog, holds the keyboard
page_typing() {
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=settings/page"* && "$line" == *"want_text=1"* ]]
}
posted() { # <app> [critical]: post one and wait for the notifier to take it
    local before
    before=$(grep -c "control: notification" "$IMWAY_LOG" || true)
    ctl "notify $1 0 ${2:-0} from-$1"
    # the count is re-read on every try: await runs its command, not its value
    taken() { (( $(grep -c "control: notification" "$IMWAY_LOG" || true) > before )); }
    await 50 taken || { echo "notify $1 was not taken"; exit 1; }
}
await_active() { # <count> <why>
    await 50 active_is "$1" || { echo "$2: $(active) toasts on screen, expected $1"; dump_state; exit 1; }
}

# a combo's popup is its own window under the combo; items one row apart
combo_pick() { # <dx> <dy> <item>
    local px py
    at "$1" "$2"
    combo_open() { [[ -n "$(dump_field '^imgui name=##Combo' x)" ]]; }
    await 50 combo_open || { echo "the combo at $1,$2 did not open"; dump_state; exit 1; }
    px=$(dump_field '^imgui name=##Combo' x); py=$(dump_field '^imgui name=##Combo' y)
    click_at $((px + 30)) $((py + 17 + $3 * 20))
    combo_closed() { [[ -z "$(dump_field '^imgui name=##Combo' x)" ]]; }
    await 50 combo_closed || { echo "the combo stayed open"; exit 1; }
    ctl "motion 1000 700"
}

# the muted application stays off screen, another one shows
posted muteme
await_active 0 "a muted application's toast"
posted other
await_active 1 "an unruled application's toast"

# the rule's policy combo: allow
combo_pick 670 279 1
posted muteme
await 50 active_is 2 || { echo "the allow policy did not let muteme through"; dump_state; exit 1; }

# type into the rule's name field: ImGui trickles the characters in one a
# frame, so wait until the field holds all of them before anything that
# would take the keyboard away from it
type_rule() { # <text> <what the field then holds>
    at 400 279
    await 100 page_typing || { echo "the rule's name field did not take the keyboard"; exit 1; }
    ctl "type $1"
    await_input "$2" || { echo "typing '$1' did not make the field '$2'"; dump_state | grep '^imgui input' || true; exit 1; }
    at 500 380 # the empty page: the field lets go
}

# back to mute, then rename the rule: the old name is free again
combo_pick 670 279 2
type_rule x mutemex
posted muteme
await_active 3 "muteme after the rule moved to mutemex"
posted mutemex
sleep 0.5
active_is 3 || { echo "the renamed rule did not mute mutemex"; exit 1; }

# remove it, and the renamed application shows
at 742 279
posted mutemex
await_active 4 "mutemex after its rule was removed"

# a new rule from the button: name it and mute it
at 255 279 # "add application rule" moved up into the removed row's place
type_rule addme addme
combo_pick 670 279 2
posted addme
sleep 0.5
active_is 4 || { echo "the added rule did not mute addme"; exit 1; }

expect_alive "compositor died under the notification settings"
echo "OK: the notification page's widgets shape the toasts and the per-application rules"
