#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --dpms 1
# The idle timeout locks before the display goes off; on a session the
# user already locked it finds the lock screen up and leaves it alone:
# after the wake there is one lock screen, and one password unlocks it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
captured() { [[ "$(dump_field '^captured ' kb)" == "$1" ]]; }
await 50 captured 1 || { echo "Super+L did not lock"; dump_state; exit 1; }

await 100 in_log "display off (idle)" || { echo "the display never went idle"; cat "$IMWAY_LOG"; exit 1; }
ctl "motion 100 100"
await 100 in_log "display back on" || { echo "input did not wake the display"; cat "$IMWAY_LOG"; exit 1; }

overlays() { dump_state | grep -c '^imgui name=##lock-overlay ' || true; }
[[ "$(overlays)" == 1 ]] || { echo "the idle lock left $(overlays) lock screens"; dump_state; exit 1; }

unlock() {
    await_typing '##lock-overlay' || return 1
    ctl "type xxx"
    await_input "xxx" || return 1
    ctl "key 28 press"; ctl "key 28 release"
    await 30 in_log "lockscreen closed"
}
await 3 unlock || { echo "the password did not unlock"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died idling a locked session"
echo "OK: the idle lock leaves a locked session's lock screen alone"
