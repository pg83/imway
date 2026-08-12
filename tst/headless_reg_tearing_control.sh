#!/usr/bin/env bash
# wp-tearing-control: the async hint must be recorded on the surface (the
# async page flip itself is hardware-only and unverifiable headless).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "async"
sleep 0.2

t=$(dump_field 'app_id=tearing' tearing)
[[ "${t:-0}" -eq 1 ]] || { echo "tearing hint not recorded (got '$t')"; exit 1; }
expect_alive

echo "OK: wp-tearing-control async hint recorded"
