#!/usr/bin/env bash
# imway-args: --hdr 203
# An output colour change seen by a colour-management v1 client: its live
# surface feedback gets the v1 preferred_changed, one whose surface died
# gets nothing, and its v1 wl_output (no done event) is skipped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "waiting for change"
ctl "sdr-white 120"
wait_client "feedback v1 done"
expect_client_ok "the v1 colour feedback heard the wrong events"
echo "OK: a v1 colour client heard the change the v1 way"
