#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

before=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
"$IMWAY_CLIENT"
sleep 0.3
after=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
[[ $after -le $((before + 4)) ]] || {
    echo "compositor leaked fds over the shm churn: before=$before after=$after"
    exit 1
}
expect_alive "compositor died on the fd churn"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after the fd churn"
