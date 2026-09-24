#!/usr/bin/env bash
# The glue that runs libpulse on the compositor's loop honours the whole
# pa_mainloop_api contract, the parts libpulse's client code does not lean
# on included: a freed event calls the destroy hook set on it once, a null
# time disarms a timer, and quit leaves the compositor's loop running.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "pulse-mainloop-conformance"
await 100 in_log "control: pulse mainloop conformance" || { echo "the conformance run did not report"; cat "$IMWAY_LOG"; exit 1; }
if in_log "control: pulse mainloop conformance, -1 failed"; then
    echo "SKIP: built without libpulse"
    exit 127
fi
in_log "control: pulse mainloop conformance, 0 failed" || { echo "the pulse mainloop glue broke its contract"; grep "pulse mainloop" "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died under the pulse mainloop conformance run"
echo "OK: the pulse mainloop glue honours the pa_mainloop_api contract"
