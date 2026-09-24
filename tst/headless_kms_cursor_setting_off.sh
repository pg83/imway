#!/usr/bin/env bash
# The hardware cursor turned off while the cursor rides its plane: the
# cursor is composited into the frame from then on, and the plane goes
# dark rather than keep showing the last image beside it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "motion 200 200"
compose_frame
await 100 in_log "fake-kms: cursor plane on" || { echo "the cursor never reached its plane"; cat "$IMWAY_LOG"; exit 1; }

ctl "set advanced.hardware_cursor false"
await 100 in_log "control: set advanced.hardware_cursor" || { echo "settings are not reachable"; exit 1; }
for i in $(seq 1 20); do
    ctl "motion $((200 + i * 4)) 200"
    in_log "fake-kms: cursor plane off" && break
    sleep 0.05
done
await 50 in_log "fake-kms: cursor plane off" || { echo "the plane kept its cursor after the hardware cursor was turned off"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died turning the hardware cursor off"
echo "OK: turning the hardware cursor off takes the cursor off its plane"
