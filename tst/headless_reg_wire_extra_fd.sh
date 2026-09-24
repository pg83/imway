#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

fd_baseline "$XDG_RUNTIME_DIR/fds.txt"
for _ in $(seq 1 32); do
    "$IMWAY_CLIENT"
done
expect_fds_kept "$XDG_RUNTIME_DIR/fds.txt" 2 "on extra SCM_RIGHTS fds"
expect_alive "compositor died on an extra SCM_RIGHTS fd"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after extra-fd clients"
