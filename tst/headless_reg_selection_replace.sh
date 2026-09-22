#!/usr/bin/env bash
# A selection replaced by the next one cancels the source it displaces, for
# wl_data_device, primary selection and data-control sources alike.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "selection replace done"
expect_client_ok "a displaced selection source was not cancelled"
expect_alive "compositor died replacing selections"
echo "OK: displaced selection sources were cancelled"
