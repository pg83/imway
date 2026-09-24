#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

fd_baseline "$XDG_RUNTIME_DIR/fds.txt"
for _ in $(seq 1 64); do
    "$IMWAY_CLIENT"
done
expect_fds_kept "$XDG_RUNTIME_DIR/fds.txt" 2 "cleaning unexpected fds"
expect_alive "compositor died while cleaning unexpected fds"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
