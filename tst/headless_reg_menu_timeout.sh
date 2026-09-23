#!/usr/bin/env bash
# private-session-bus
# A window menu whose first layout call is never answered: the call times
# out on the compositor's loop after three seconds and the menu stays
# empty, a late answer to it is discarded, and the next LayoutUpdated is
# served and fills the menu.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

menu_field() { dump_field '^menu appmenu=' "$1"; }

start_client
wait_client "layout held"
[[ "$(menu_field ready)" == 0 ]] || { echo "the menu was ready without a layout"; dump_state; exit 1; }

wait_client "timeout passed"
wait_client "layout served"
filled() { dump_state | grep -q '^menuitem .* label=Fresh$'; }
await 100 filled || { echo "the layout served after the timeout did not fill the menu"; dump_state; cat "$CLIENT_LOG"; exit 1; }
! dump_state | grep -q 'label=Stale' || { echo "the late answer to the timed-out call was taken"; dump_state; exit 1; }

expect_alive "compositor died when a layout call timed out"
echo "OK: an unanswered layout call times out and the menu recovers"
