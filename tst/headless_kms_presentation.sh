#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# presentation-time on the KMS output: frames are reported with the page
# flip's own timestamp and sequence, flagged as vsynced hardware timing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "hardware presented"
expect_client_ok "no hardware-timed presentation feedback"

expect_alive "compositor died reporting presentation"
echo "OK: presentation feedback carries the page flip's timing"
