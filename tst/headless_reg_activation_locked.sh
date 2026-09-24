#!/usr/bin/env bash
# A window activated with a token taken before the screen locked: the
# activation lands while the lock screen is up, and the keys typed at the
# lock screen still reach no client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "activation-locked: mapped"
wait_rect 'app_id=activation-locked-a'
for _ in $(seq 1 20); do
    ctl "key 30 press"; ctl "key 30 release"
    grep -q "token ready" "$CLIENT_LOG" && break
    sleep 0.2
done
wait_client "token ready"

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_typing '##lock-overlay' || { echo "the lock screen never took the keyboard"; dump_state; exit 1; }

touch go-activate
wait_client "activation-locked: activated"
await 50 in_log "imway: activation (" || { echo "the activation under the lock screen was not taken"; cat "$IMWAY_LOG"; exit 1; }
ctl "type abc"
sleep 0.5
touch go-report
wait_client "keys while locked"
grep -q "keys while locked 0" "$CLIENT_LOG" || { echo "keys typed at the lock screen reached a client"; cat "$CLIENT_LOG"; exit 1; }
await_typing '##lock-overlay' || { echo "the activation took the keyboard from the lock screen"; dump_state; exit 1; }

expect_alive "compositor died activating a window under the lock screen"
echo "OK: an activation under the lock screen does not let keys reach the client"
