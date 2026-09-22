#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The tearing setting overrides the client's wish either way: "always"
# presents a direct-scanout client that never asked for tearing with async
# page flips, "deny" keeps a client that asked for it on vsynced flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

asyncs() {
    grep -c "fake-kms: async page flip" "$IMWAY_LOG" || true
}
flips() { dump_field '^kms' flips; }
candidate() { # <title>
    [[ "$(dump_field '^scanout' candidate)" == "$(dump_field "title=$1" id)" && -n "$(dump_field "title=$1" id)" ]]
}
nudge_flips() { # <count>: drive at least that many more flips
    local f0 i
    f0=$(flips)
    for i in $(seq 1 100); do
        [[ "$(flips)" -ge "$((f0 + $1))" ]] && return 0
        ctl "motion $((200 + i % 50)) 300"
        sleep 0.05
    done
    return 1
}

ctl "set advanced.tearing 2" # always
await 20 in_log "control: set advanced.tearing" || { echo "settings are not reachable"; exit 1; }
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_direct_scanout"
start_client
wait_client "taint candidate mapped"
await 100 candidate kms-taint || { echo "the client never reached the plane"; dump_state; exit 1; }
has_async() { [[ "$(asyncs)" -gt 0 ]]; }
nudge_flips 5 || { echo "flips stopped"; exit 1; }
await 50 has_async || { echo "\"always\" did not tear a client that never asked"; cat "$IMWAY_LOG"; exit 1; }
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true

ctl "set advanced.tearing 0" # deny
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_kms_tearing"
start_client
wait_client "tearing candidate mapped"
await 100 candidate kms-tearing || { echo "the tearing client never reached the plane"; dump_state; exit 1; }
a0=$(asyncs)
nudge_flips 10 || { echo "flips stopped under deny"; exit 1; }
[[ "$(asyncs)" == "$a0" ]] || { echo "\"deny\" still tore for the client that asked"; exit 1; }

expect_alive "compositor died switching the tearing policy"
echo "OK: the tearing policy forces async flips on or off"
