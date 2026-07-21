#!/usr/bin/env bash
# wl_compositor v6: preferred_buffer_scale/transform events and the v5 offset
# request.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "wl_compositor v6 contract not met"
expect_alive

echo "OK: wl_compositor v6"
