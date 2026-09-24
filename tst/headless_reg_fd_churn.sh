#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

fd_baseline "$XDG_RUNTIME_DIR/fds.txt"
"$IMWAY_CLIENT"
sleep 0.3
expect_fds_kept "$XDG_RUNTIME_DIR/fds.txt" 4 "over the shm churn"
expect_alive "compositor died on the fd churn"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after the fd churn"
