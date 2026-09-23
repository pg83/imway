#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=entropy=3
# xdg-foreign early at boot, before getrandom has entropy: every export's
# handle takes its random part from the clock and a counter instead (the
# log says so for each of the client's three exports), and the handles
# still import, parent, revoke and import dead as with entropy
# (headless_reg_xdg_foreign).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_xdg_foreign"
start_client
wait_client "attached"
wait_mapped 'app_id=foreign-child'

clock_handles() { grep -c "imway: xdg-foreign: no entropy yet, handle for toplevel" "$IMWAY_LOG" || true; }
[[ "$(clock_handles)" -ge 1 ]] || { echo "the export took no clock handle"; cat "$IMWAY_LOG"; exit 1; }

parent_is() { [[ "$(dump_field 'app_id=foreign-child' parent)" == "$1" ]]; }
pid=$(dump_field 'app_id=foreign-parent' id)
await 30 parent_is "$pid" || { echo "child not attached through a clock handle: parent=$(dump_field 'app_id=foreign-child' parent) expected=$pid"; exit 1; }

# revoke the export
ctl "key 57 press"
ctl "key 57 release"
wait_client "revoked"
await 30 parent_is 0 || { echo "parent link survived the revoke"; exit 1; }

wait_client "dead import"
[[ "$(clock_handles)" -eq 3 ]] || { echo "expected 3 clock handles, got $(clock_handles)"; cat "$IMWAY_LOG"; exit 1; }
kill -0 "$CLIENT_PID" || { echo "the xdg-foreign client died on clock handles"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "the compositor died making clock handles"
echo "OK: xdg-foreign handles made without entropy still import"
