#!/usr/bin/env bash
# A same-client drag may omit wl_data_source. It must enter and drop normally,
# and the compositor must survive the release path.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "null-source ready"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.3

ctl "button left press"
wait_client "null-source started"
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x + 5)) $((y + 5))"
wait_client "null-source entered"
ctl "button left release"

expect_client_ok "source-less drag did not complete"
expect_alive "source-less drag killed the compositor"
echo "OK: source-less drag entered and dropped without crashing"
