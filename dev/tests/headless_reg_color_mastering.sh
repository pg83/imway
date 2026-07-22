#!/usr/bin/env bash
# #G-15: color-management advertises and accepts mastering metadata,
# tf_power and the extra named primaries.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "color-mastering done"
expect_client_ok "the mastering/tf_power image description was rejected"
echo "OK: color-management accepts mastering metadata, tf_power and named primaries"
