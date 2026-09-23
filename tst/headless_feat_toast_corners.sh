#!/usr/bin/env bash
# Toasts in each of the four corners the setting names: a posted toast sits
# against that corner of the output, below the top bar when on top; do
# not disturb takes it off the screen before the next corner.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.timeout 60"
toast() { dump_field '^imgui name=##toast' "$1"; }
n=0
corner() { # <ordinal> <name> <check>
    ctl "set notifications.position $1"
    n=$((n + 1))
    ctl "notify corner-$n 0 0 toast-$n"
    placed() {
        local x y w h
        x=$(toast x); y=$(toast y); w=$(toast w); h=$(toast h)
        [[ -n "$x" ]] || return 1
        eval "$corner_check"
    }
    corner_check=$3
    await 50 placed || { echo "the toast is not in the $2 corner: $(dump_state | grep '^imgui name=##toast')"; exit 1; }
    ctl "set notifications.dnd true"
    none() { [[ -z "$(toast x)" ]]; }
    await 50 none || { echo "do not disturb left a toast up"; exit 1; }
    ctl "set notifications.dnd false"
}

corner 0 "top right" '(( x + w >= 1266 && y <= 40 ))'
corner 1 "top left" '(( x <= 14 && y <= 40 ))'
corner 2 "bottom right" '(( x + w >= 1266 && y + h >= 786 ))'
corner 3 "bottom left" '(( x <= 14 && y + h >= 786 ))'

expect_alive "compositor died placing toasts"
echo "OK: toasts sit in each of the four corners"
