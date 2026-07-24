#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# A mode-list change without a disconnect (a dock re-reading EDID): the
# hotplug probe notices the current mode is gone and remodesets in place,
# never reporting the connector down.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "tv client sized 1280x800"

tlid=$(dump_field 'title=kms-tv' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "no direct scanout at boot"; dump_state; exit 1; }

# the connector stays up; only the mode list changes under the probe
ctl "kms-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "live mode-list change not followed"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "connector disconnected" || { echo "a live change went through a disconnect"; exit 1; }

wait_client "tv client sized 1920x1080"
await 100 candidate || { echo "no direct scanout after the live change"; dump_state; exit 1; }

kill -0 "$CLIENT_PID" || { echo "client died on a live mode change"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died on a live mode change"
echo "OK: a connected-state mode change remodesets in place"
