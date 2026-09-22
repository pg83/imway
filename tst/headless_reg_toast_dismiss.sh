#!/usr/bin/env bash
# A click on a toast dismisses it: it leaves the screen and stays in the
# history. A critical one, which never expires on its own, goes the same
# way.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

toast_x() { dump_field '^imgui name=##toast' x; }
toast_up() { [[ -n "$(toast_x)" ]]; }
active_is() { [[ "$(dump_field '^notifications' active)" == "$1" ]]; }

ctl "notify imway-test 0 1 click to dismiss"
await 100 toast_up || { echo "the toast did not show"; dump_state; exit 1; }

x=$(toast_x); y=$(dump_field '^imgui name=##toast' y)
w=$(dump_field '^imgui name=##toast' w); h=$(dump_field '^imgui name=##toast' h)
click_at $((x + w / 2)) $((y + h / 2))

await 100 active_is 0 || { echo "the clicked toast stayed on screen"; dump_state; exit 1; }
[[ "$(dump_field '^notifications' history)" == 1 ]] || { echo "the dismissed toast left the history"; dump_state; exit 1; }
expect_alive "compositor died dismissing a toast"
echo "OK: a clicked toast is dismissed into the history"
