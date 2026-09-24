#!/usr/bin/env bash
# A hotplug probe that finds the connector object itself gone for a moment
# (an MST port re-enumerating): the probe carries no information, so the
# output neither disconnects nor remodesets, and when the connector is
# back as it was the session just keeps flipping.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

ctl "kms-connector 2"
dump_state >/dev/null
sleep 0.3
! in_log "connector disconnected" || { echo "a vanished connector object was taken for an unplug"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 1"
dump_state >/dev/null
sleep 0.3
! in_log "remodeset" || { echo "an unchanged connector was remodeset"; cat "$IMWAY_LOG"; exit 1; }

f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the connector came back"; exit 1; }

expect_alive "compositor died on a vanished connector"
echo "OK: a probe of a vanished connector changes nothing"
