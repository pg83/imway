#!/usr/bin/env bash
# Removing an application rule from above another in the notifications
# settings page: the one below slides up into its place and keeps muting its
# application, while the removed rule's application shows again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set notifications.timeout 60"
ctl "rule 0 2 firstapp"
ctl "rule 1 2 slideme"
await 100 in_log "control: rule 1" || { echo "the rules did not land"; exit 1; }

active() { dump_field '^notifications ' active; }
active_is() { [[ "$(active)" == "$1" ]]; }
posted() { # <app>: post one and wait for the notifier to take it
    local before
    before=$(grep -c "control: notification" "$IMWAY_LOG" || true)
    ctl "notify $1 0 0 from-$1"
    taken() { (( $(grep -c "control: notification" "$IMWAY_LOG" || true) > before )); }
    await 50 taken || { echo "notify $1 was not taken"; exit 1; }
}

posted firstapp
posted slideme
sleep 0.5
active_is 0 || { echo "the rules did not mute both applications"; dump_state; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "launcher did not open"; exit 1; }
ctl "type settings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }
await_imgui settings || { echo "settings did not open"; exit 1; }
wx=$(dump_field '^imgui name=settings ' x); wy=$(dump_field '^imgui name=settings ' y)
click_at $((wx + 40)) $((wy + 38 + 7 * 20)) # the notifications page
ctl "motion 1000 700"
sleep 0.3

# the first rule's remove button, as in the rules scenario
click_at $((wx + 742)) $((wy + 279))
posted firstapp
await 50 active_is 1 || { echo "the removed rule still mutes firstapp: $(active) on screen"; dump_state; exit 1; }
posted slideme
sleep 0.5
active_is 1 || { echo "the rule that slid up no longer mutes slideme"; exit 1; }

expect_alive "compositor died removing a rule"
echo "OK: removing a rule slides the next one up intact"
