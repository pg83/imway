#!/usr/bin/env bash
# zwp_linux_dmabuf_v1 v5.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "linux-dmabuf v5 contract not met"
expect_alive

echo "OK: zwp_linux_dmabuf_v1 v5"
