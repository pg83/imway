#!/usr/bin/env bash
# imway-args: --dpms 1
# The idle timeout locks the session before it switches the display off:
# the display goes dark with the lock screen already up, input wakes it
# onto that lock screen, and the password typed there unlocks.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 in_log "display off (idle)" || { echo "the display never went idle"; cat "$IMWAY_LOG"; exit 1; }

ctl "motion 100 100"
await 100 in_log "display back on" || { echo "input did not wake the display"; cat "$IMWAY_LOG"; exit 1; }

locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
await 100 locked || { echo "the display woke onto an unlocked session"; dump_state; exit 1; }

for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed" && break
done
in_log "lockscreen closed" || { echo "the lock screen did not take input after the idle"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died idling a locked session"
echo "OK: idle locks before the display goes off, and wakes onto the lock"
