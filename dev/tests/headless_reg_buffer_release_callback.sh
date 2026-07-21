#!/usr/bin/env bash
# wl_surface v7 get_release per-commit buffer release callback.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "wl_surface get_release contract not met"
expect_alive

echo "OK: wl_surface v7 get_release"
