#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A VT comeback whose remodeset the device will not take. A test commit
# bouncing with EPERM or EBUSY says nothing about the configuration: the
# remodeset is called unavailable and nothing is degraded. A test that
# passes and a commit that fails with and without the cursor plane is a
# failed modeset. Each leaves the session to the next comeback, which
# relights and flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

comeback() { # <fault> — away, arm the fault while nothing commits, back
    local away
    away=$(grep -c "session disabled (vt switch away)" "$IMWAY_LOG" || true)
    ctl "session 0"
    disabled() { [[ "$(grep -c "session disabled (vt switch away)" "$IMWAY_LOG" || true)" -gt "$away" ]]; }
    await 50 disabled || { echo "session did not disable"; exit 1; }
    sleep 0.3
    ctl "kms-fail-commit $1"
    dump_state >/dev/null
    ctl "session 1"
}

comeback "1 1 1"
await 100 in_log "modeset commit unavailable, errno 1$" || { echo "an EPERM test commit was not taken as unavailable"; cat "$IMWAY_LOG"; exit 1; }

comeback "16 1 1"
await 100 in_log "modeset commit unavailable, errno 16$" || { echo "an EBUSY test commit was not taken as unavailable"; cat "$IMWAY_LOG"; exit 1; }

comeback "22 2 0"
await 100 in_log "kms atomic commit failed, errno 22 (modeset)" || { echo "the failed modeset commit was not reported"; cat "$IMWAY_LOG"; exit 1; }

! in_log "session enabled, remodeset" || { echo "a refused comeback was reported relit"; cat "$IMWAY_LOG"; exit 1; }
! in_log "software cursor" || { echo "a refused comeback gave up the cursor plane"; cat "$IMWAY_LOG"; exit 1; }

comeback "0 0 0"
await 100 in_log "session enabled, remodeset" || { echo "the clean comeback did not relight"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the comeback"; exit 1; }

expect_alive "compositor died on a refused comeback"
echo "OK: a refused comeback leaves the configuration alone and the next one relights"
