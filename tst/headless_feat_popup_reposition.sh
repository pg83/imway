#!/usr/bin/env bash
# xdg_popup.reposition: the popup moves from the parent's corner to
# (110,110), with repositioned(token) preceding the new configure.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

popup_at() { # <x> <y>
    [[ $(dump_field '^popup' x) == "$1" && $(dump_field '^popup' y) == "$2" ]]
}

start_client
wait_client "popup mapped"
sleep 0.3
await 20 popup_at 10 10 || {
    echo "initial popup at $(dump_field '^popup' x),$(dump_field '^popup' y), want 10,10"; exit 1; }

ctl "key 2 press"; ctl "key 2 release"   # KEY_1
wait_client "repositioned token=7"
wait_client "reposition ok"
await 20 popup_at 110 110 || {
    echo "popup did not move: $(dump_field '^popup' x),$(dump_field '^popup' y), want 110,110"; exit 1; }

expect_alive "compositor died on reposition"
echo "OK: reposition moved the popup with the token round-trip"
