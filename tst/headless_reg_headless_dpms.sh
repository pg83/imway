#!/usr/bin/env bash
# imway-args: --dpms 1
# Idle power management on an output without a power state: the headless
# display cannot switch off, but the idle timeout still locks the session
# first, and input afterwards wakes it and reaches the lock screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

locked() {
    [[ "$(dump_field '^captured ' kb)" = 1 ]]
}
await 100 locked || { echo "the idle timeout did not lock"; dump_state; exit 1; }

ctl "motion 100 100"
for _ in 1 2 3 4 5; do
    sleep 0.5
    ctl "type xxx"
    sleep 0.5
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed" && break
done
in_log "lockscreen closed" || { echo "the lock screen did not take input after the idle"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died idling a headless output"
echo "OK: idle locks and wakes a headless session"
