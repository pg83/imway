#!/usr/bin/env bash
# The notifications settings page, driven by the pointer, and the toasts it
# shapes. The toast width slider and the position combo move and resize the
# toasts on screen (bottom left stacks them upward from the corner), a
# critical toast wears its red border and a click dismisses a toast. The
# history slider at its left end keeps nothing off screen. An application rule's
# policy combo, its name field and its remove button decide which of an
# application's notifications reach the screen, and "add application rule"
# makes a new one. Coordinates are relative to the settings window, whose
# page rows are one framed widget apart.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.timeout 60"
ctl "rule 0 2 muteme" # mute
await 20 in_log "control: rule 0" || { echo "the rule did not land"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
at() { # <dx> <dy>: click inside the settings window
    click_at $((wx + $1)) $((wy + $2))
}
at 40 $((38 + 7 * 20)) # the notifications page
ctl "motion 1000 700"

active() { dump_field '^notifications ' active; }
history() { dump_field '^notifications ' history; }
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
    await 50 test "$(grep -c "control: notification" "$IMWAY_LOG" || true)" -gt "$before" || { echo "notify $1 was not taken"; exit 1; }
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

# toast width: the slider spans 200..800 px, click three quarters along
at $((366 + (750 - 366) * 3 / 4)) 147
wide() { (( $(dump_field '^imgui name=##toast' w) >= 600 )); }
await 50 wide || { echo "the width slider did not widen the toast"; dump_state; exit 1; }

# position: bottom left, the fourth item; a second toast stacks above
combo_pick 500 173 3
posted second
await_active 2 "two toasts"
bottom_left() {
    local xs ys hs
    xs=$(dump_state | awk '$1 == "imgui" && $2 ~ /^name=##toast/ { for (i = 1; i <= NF; i++) if ($i ~ /^x=/) print substr($i, 3) }' | sort -n | head -1)
    read -r ys hs < <(dump_state | awk '$1 == "imgui" && $2 ~ /^name=##toast/ { y = h = 0; for (i = 1; i <= NF; i++) { if ($i ~ /^y=/) y = substr($i, 3); if ($i ~ /^h=/) h = substr($i, 3) } if (y + h > m) { m = y + h; yy = y; hh = h } } END { print yy, hh }')
    [[ "$xs" == 8 ]] && (( ys + hs >= 790 ))
}
await 50 bottom_left || { echo "the toasts did not move to the bottom left"; dump_state; exit 1; }

# a critical toast is framed in red
posted crit 1
await_active 3 "a critical toast"
red_border() {
    screenshot "$XDG_RUNTIME_DIR/crit.ppm" && centroid "$XDG_RUNTIME_DIR/crit.ppm" 220 90 60 >/dev/null 2>&1
}
await 50 red_border || { echo "the critical toast has no red border"; exit 1; }

# a click on a toast dismisses it: the lowest one sits in the corner
click_at 100 780
await_active 2 "after a click on a toast"

# history at its left end: the next post keeps nothing but what is on
# screen, the muted one it adds included
at 368 121
sleep 0.3
posted muteme
kept_only_active() { [[ "$(history)" == "$(active)" ]]; }
await 50 kept_only_active || { echo "the history slider at 0 still keeps $(history) with $(active) on screen"; exit 1; }

# the rule's policy combo: allow
combo_pick 670 279 1
posted muteme
await 50 active_is 3 || { echo "the allow policy did not let muteme through"; dump_state; exit 1; }

# type into the rule's name field: ImGui trickles the characters in one a
# frame, so wait for the field to change and then hold still before anything
# that would take the keyboard away from it
field_diff() { # <a> <b>
    region_diff "$XDG_RUNTIME_DIR/$1.ppm" "$XDG_RUNTIME_DIR/$2.ppm" $((wx + 166)) $((wy + 268)) $((wx + 620)) $((wy + 290))
}
type_rule() { # <text>
    at 400 279
    await 100 page_typing || { echo "the rule's name field did not take the keyboard"; exit 1; }
    screenshot "$XDG_RUNTIME_DIR/before.ppm"
    ctl "type $1"
    settled() {
        screenshot "$XDG_RUNTIME_DIR/a.ppm" && sleep 0.3 && screenshot "$XDG_RUNTIME_DIR/b.ppm" &&
            [[ "$(field_diff before b)" -gt 0 && "$(field_diff a b)" -eq 0 ]]
    }
    await 50 settled || { echo "typing '$1' did not reach the field"; exit 1; }
    at 500 380 # the empty page, clear of the toast stack: the field lets go
}

# back to mute, then rename the rule: the old name is free again
combo_pick 670 279 2
type_rule x
posted muteme
await_active 4 "muteme after the rule moved to mutemex"
posted mutemex
sleep 0.5
active_is 4 || { echo "the renamed rule did not mute mutemex"; exit 1; }

# remove it, and the renamed application shows
at 742 279
posted mutemex
await_active 5 "mutemex after its rule was removed"

# a new rule from the button: name it and mute it
at 255 279 # "add application rule" moved up into the removed row's place
type_rule addme
combo_pick 670 279 2
posted addme
sleep 0.5
active_is 5 || { echo "the added rule did not mute addme"; exit 1; }

expect_alive "compositor died under the notification settings"
echo "OK: the notification page's widgets shape the toasts and the per-application rules"
