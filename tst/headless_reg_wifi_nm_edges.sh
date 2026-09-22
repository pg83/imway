#!/usr/bin/env bash
# private-session-bus
# expect-compositor-exit
# imway-env: IMWAY_SETTINGS=applications.wifi_backend=2
# imway-pre: "$IMWAY_CLIENT" >"$XDG_RUNTIME_DIR/nm.log" 2>&1 & for i in $(seq 50); do grep -q "nm ready" "$XDG_RUNTIME_DIR/nm.log" 2>/dev/null && exit 0; sleep 0.1; done; echo "fake NetworkManager did not come up"; exit 1
# A misbehaving NetworkManager, asked for by the backend setting: a refused
# device list leaves wifi unavailable and scanning a no-op; a change that
# lands mid-refresh runs the walk again once it finishes; failing devices,
# connections and access points, a missing or mistyped SSID and a byte
# strength are all survived; an emptied device list drops the network with
# a toast; integer-keyed property dicts are skipped, not read as names; and
# the compositor shuts down with a read still unanswered.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

NM_LOG="$XDG_RUNTIME_DIR/nm.log"

nm() { grep -q "$1" "$NM_LOG"; }

wifi_is() { # <state> <networks>
    [[ "$(dump_field '^wifi state' state)" == "$1" && "$(dump_field '^wifi state' networks)" == "$2" ]]
}

history_is() { [[ "$(dump_field '^notifications' history)" == "$1" ]]; }

fail() {
    echo "$1"
    dump_state
    cat "$NM_LOG"
    exit 1
}

await 150 in_log "wifi via NetworkManager" || fail "NetworkManager was not picked"
await 150 nm "devices 1" || fail "the device list was never asked for"
await 100 wifi_is 0 0 || fail "a refused device list did not leave wifi unavailable"

# the picker's scan button has no device to scan with
x0=$(dump_field '^wifi glyph' x0); y0=$(dump_field '^wifi glyph' y0)
x1=$(dump_field '^wifi glyph' x1); y1=$(dump_field '^wifi glyph' y1)
click_at $(((x0 + x1) / 2)) $(((y0 + y1) / 2))
picker_open() { [[ -n "$(dump_field '^imgui name=##wifi' x)" ]]; }
await 50 picker_open || fail "the wifi picker did not open"
sleep 0.3
x=$(dump_field '^imgui name=##wifi' x); y=$(dump_field '^imgui name=##wifi' y); w=$(dump_field '^imgui name=##wifi' w)
click_at $((x + w - 8 - 14)) $((y + 8 + 6))
ctl "key 1 press"; ctl "key 1 release"

# one change, and a second while its refresh is in flight: two walks
touch "$XDG_RUNTIME_DIR/go-1"
await 150 nm "devices 3" || fail "the change during the refresh was lost"
await 100 wifi_is 4 1 || fail "the surviving access point did not connect"
line=$(dump_state | grep '^wifinet ')
[[ "$line" == *"strength=70 "* && "$line" == *"connected=1 "* && "$line" == *"type=open "* && "$line" == *"name=edge-one" ]] || fail "the access point was misread: $line"
await 50 history_is 1 || fail "connecting posted no toast"
nm "settings /org/freedesktop/NetworkManager/Settings/3" || fail "the saved connections were not all read"
! nm "scan requested" || fail "a scan went out without a device"

# the device list empties
touch "$XDG_RUNTIME_DIR/go-2"
await 150 nm "devices 4" || fail "the second change was not followed"
await 100 wifi_is 0 0 || fail "an empty device list kept the network"
await 50 history_is 2 || fail "losing the network posted no toast"

# property dicts keyed by integers
touch "$XDG_RUNTIME_DIR/go-3"
await 150 nm "get-all 5 /org/freedesktop/NetworkManager/AccessPoint/9" || fail "the integer-keyed round never ran"
await 100 wifi_is 4 0 || fail "integer keys were read as properties"
expect_alive "compositor died on integer-keyed NetworkManager properties"

# a read left unanswered, then shutdown
touch "$XDG_RUNTIME_DIR/go-4"
await 150 nm "wireless held" || fail "the last round never reached the wireless read"
ctl quit
await 100 in_log "clean exit" || fail "the compositor did not shut down with a read pending"
echo "OK: NetworkManager errors, gaps, mistypes and a pending read at shutdown"
