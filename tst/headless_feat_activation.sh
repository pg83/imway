#!/usr/bin/env bash
# xdg-activation: a token activates a background toplevel, moving focus to it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
# the key is the token's serial, and a serial is only good for the focus it
# was sent under: inject it once A, mapped last, holds the focus, or a key
# landing on B first goes stale when A takes over
a_focused() {
    [[ "$(dump_field 'title=client_feat_activation_A' focused)" == 1 ]]
}
await 100 a_focused || { echo "A never took the focus"; dump_state; exit 1; }
ctl "key 30 press"   # a serial for the activation token
ctl "key 30 release"

expect_client_ok "activation did not move focus"
echo "OK: xdg-activation moved keyboard focus to the activated toplevel"
