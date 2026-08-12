#!/usr/bin/env bash
# wl_data_device_manager v4 (release request).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "data_device_manager v4 contract not met"
expect_alive

echo "OK: wl_data_device_manager v4"
