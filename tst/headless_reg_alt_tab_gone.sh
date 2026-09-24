#!/usr/bin/env bash
# The window alt-tab selected goes away before the Tab key comes up (Tab
# up is what raises the selection). Unmapped, it is not raised, and the
# focus stays where it was. Destroyed, the release raises nothing, and the
# next Tab starts from nothing selected and finds no mapped window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "two toplevels mapped"
wait_rect 'app_id=alt-tab-gone-b'
bid=$(dump_field 'app_id=alt-tab-gone-b' id)
focus_on_b() { [[ "$(dump_field '^focus ' id)" == "$bid" ]]; }
await 50 focus_on_b || { echo "the second window never took the focus"; dump_state; exit 1; }

switcher_up() { [[ "$(dump_field '^captured ' kb)" == 1 ]]; }
switcher_gone() { [[ "$(dump_field '^captured ' kb)" == 0 ]]; }

ctl "key 56 press"   # KEY_LEFTALT
ctl "key 15 press"   # KEY_TAB: selects a
await 30 switcher_up || { echo "the switcher did not come up"; dump_state; exit 1; }
touch go-unmap
wait_client "a unmapped"
ctl "key 15 release" # would raise a
ctl "key 56 release"
await 30 switcher_gone || { echo "the switcher did not go away"; dump_state; exit 1; }
focus_on_b || { echo "an unmapped selection took the focus"; dump_state; exit 1; }

ctl "key 56 press"
ctl "key 15 press"   # b, the lone mapped window
await 30 switcher_up || { echo "the switcher did not come up again"; dump_state; exit 1; }
touch go-destroy
wait_client "b destroyed"
ctl "key 15 release" # the selection is gone: nothing to raise
ctl "key 15 press"; ctl "key 15 release" # nothing selected, nothing mapped
ctl "key 56 release"
await 30 switcher_gone || { echo "the switcher stayed with nothing to show"; dump_state; exit 1; }

expect_alive "compositor died when the alt-tab selection went away"
input_health_probe
echo "OK: a selection that unmaps or dies under the switcher is not raised"
