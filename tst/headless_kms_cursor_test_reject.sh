#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The atomic test of a remodeset (a VT comeback) rejects the configuration
# with the hardware cursor on it: the test is retried without the cursor
# plane, that passes, and the session comes back on the software cursor.
# When the test fails without a cursor too, the remodeset is refused as a
# configuration the connector does not take, and flips carry on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

# the cursor is on the plane once a frame has drawn it there
ctl "motion 200 200"
ctl "key 2 press"; ctl "key 2 release"
await 100 in_log "fake-kms: cursor plane on" || { echo "the cursor never reached its plane"; cat "$IMWAY_LOG"; exit 1; }

comeback() { # <fault> — away, arm the fault while nothing commits, back
    ctl "session 0"
    await 50 in_log "session disabled (vt switch away)" || { echo "session did not disable"; exit 1; }
    sleep 0.3
    ctl "kms-fail-commit $1"
    dump_state >/dev/null
    ctl "session 1"
}

comeback "22 1 1"
await 100 in_log "cursor plane rejected by atomic test, software cursor" || { echo "no retry without the cursor"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "session enabled, remodeset" || { echo "the comeback did not modeset"; cat "$IMWAY_LOG"; exit 1; }

f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 300 300"
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips on the software cursor"; exit 1; }

comeback "22 1 1"
await 100 in_log "atomic test modeset rejected color/link configuration, errno 22" || { echo "the refused test was not reported"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "session enabled, remodeset" "$IMWAY_LOG")" -eq 1 ]] || { echo "a refused remodeset was reported done"; exit 1; }

f1=$(flips)
advanced() { [[ "$(flips)" -gt "$f1" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the refused remodeset"; exit 1; }

expect_alive "compositor died on a refused modeset test"
echo "OK: the modeset test sheds the cursor plane first, then refuses cleanly"
