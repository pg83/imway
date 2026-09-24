#!/usr/bin/env bash
# Hotplug probes of a connector whose mode list says little. With no modes
# at all there is nothing to follow: the output keeps its mode, a second
# unplug event while unplugged changes nothing, and replugging commits the
# mode it has, which the display refuses on record. A list longer than the
# probe keeps, with no preferred mode and the current size only at another
# refresh, is followed to its first mode.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

outputs() { grep -c "kms output: " "$IMWAY_LOG" || true; }
n0=$(outputs)

# still plugged, the mode list gone
ctl "kms-modes 4"
ctl "kms-connector 1"

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; cat "$IMWAY_LOG"; exit 1; }
ctl "kms-connector 0"
ctl "kms-connector 1"
await 50 in_log "atomic test modeset rejected color/link configuration, errno 22" || { echo "the replug without modes did not commit the mode it has"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(grep -c "connector disconnected" "$IMWAY_LOG")" == 1 ]] || { echo "an unplug event while unplugged was taken as another disconnect"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(outputs)" == "$n0" ]] || { echo "a connector without modes changed the output's mode"; cat "$IMWAY_LOG"; exit 1; }
! in_log "refuses the current mode" || { echo "a replug without modes tried another mode"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-modes 5"
ctl "kms-connector 1"
await 100 in_log "kms output: 1920x1080@60" || { echo "the long mode list was not followed to its first mode"; cat "$IMWAY_LOG"; exit 1; }
await 50 in_log "mode list changed, remodeset" || { echo "the mode list change did not remodeset"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the mode list changes"; exit 1; }

expect_alive "compositor died probing mode lists"
echo "OK: empty and long mode lists are followed as far as they say"
