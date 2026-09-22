#!/usr/bin/env bash
# private-session-bus
# expect-compositor-exit
# imway-pre: "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/iwd.log" 2>&1 & for i in $(seq 50); do grep -q "iwd ready" "$XDG_RUNTIME_DIR/iwd.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake iwd did not come up"; exit 1
# A misbehaving iwd: a refused object tree leaves wifi unavailable; a
# change landing mid-refresh walks the tree again once it finishes; an
# untracked interface, mistyped network properties, a network the tree
# never listed and out-of-range strengths are survived; object trees and
# ordered lists mistyped at every level are skipped, not read as names; a
# second passphrase request answers the first instead of stranding it;
# the agent answers Cancel, Release and methods it does not have; and the
# compositor shuts down with a prompt and an object read pending.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IWD_LOG="$XDG_RUNTIME_DIR/iwd.log"

iw() { grep -q "$1" "$IWD_LOG"; }
go() { touch "$XDG_RUNTIME_DIR/go-$1"; }

wifi_is() { # <state> <networks>
    [[ "$(dump_field '^wifi state' state)" == "$1" && "$(dump_field '^wifi state' networks)" == "$2" ]]
}

asking() { [[ "$(dump_field '^wifi state' passphrase)" == "$1" ]]; }

net() { # <path>: the dump line of one network
    dump_state | grep "^wifinet .* path=$1 " || true
}

fail() {
    echo "$1"
    dump_state
    cat "$IWD_LOG"
    exit 1
}

await 150 in_log "wifi via iwd" || fail "iwd was not detected"
await 150 iw "managed 1" || fail "the object tree was never asked for"
await 100 wifi_is 0 0 || fail "a refused tree did not leave wifi unavailable"

# one change, and a second while its walk is in flight
await 100 iw "stage tree" || fail "the fake did not reach the tree stage"
go tree
await 150 iw "ordered 3" || fail "the change during the refresh was lost"
await 100 wifi_is 4 2 || fail "roaming did not count as connected with two networks"
line=$(net /dev0/n1)
[[ "$line" == *"strength=100 "* && "$line" == *"connected=1 "* && "$line" == *"known=1 "* && "$line" == *"name=iwd-one" ]] || fail "the known network was misread: $line"
line=$(net /dev0/n2)
[[ "$line" == *"strength=0 "* && "$line" == *"connected=0 "* && "$line" == *"known=0 "* && "$line" == *"type=open "* ]] || fail "the mistyped network was misread: $line"
[[ -z "$(net /dev0/ghost)" ]] || fail "a network the tree never listed was shown"

# every level of the tree mistyped, then an ordered list with integer paths
await 100 iw "stage mistyped" || fail "the fake did not reach the mistyped stage"
go mistyped
await 150 iw "ordered 10" || fail "the mistyped rounds did not run"
await 100 wifi_is 1 0 || fail "an ordered list with integer paths produced networks"
expect_alive "compositor died on mistyped iwd objects"

# the agent: asked twice, then cancelled, released and probed
await 100 iw "stage agent" || fail "the fake did not reach the agent stage"
go agent
await 100 iw "replaced request answered" || fail "a replaced passphrase request was stranded"
await 100 asking 1 || fail "the second passphrase request did not prompt"
go asked
await 100 iw "stage cancelled" || fail "the agent calls did not complete"
iw "agent Cancel answered" || fail "Cancel was not answered"
iw "agent Release answered" || fail "Release was not answered"
iw "agent Bogus: org.freedesktop.DBus.Error.UnknownMethod" || fail "an unknown agent method was not refused"
await 100 asking 0 || fail "Cancel left the prompt up"
await 100 iw "managed held" || fail "the signal on the agent path did not refresh"
go cancelled

# pending into the shutdown
await 100 asking 1 || fail "the last passphrase request did not prompt"
go pending
await 100 iw "iwd edges done" || fail "the fake did not finish"
ctl quit
await 100 in_log "clean exit" || fail "the compositor did not shut down with calls pending"
echo "OK: iwd errors, mistypes, agent protocol and shutdown with calls pending"
