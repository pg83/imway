#!/usr/bin/env bash
# wp-content-type: the video hint must be recorded on the surface.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "set video"
sleep 0.2

ct=$(dump_field 'app_id=content-type' content_type)
[[ "${ct:-0}" -eq 2 ]] || { echo "content type not recorded (got '$ct', want 2=video)"; exit 1; }
expect_alive

echo "OK: wp-content-type video hint recorded"
