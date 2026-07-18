#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

before=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
for _ in $(seq 1 64); do
    "$IMWAY_CLIENT"
done
after=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
[[ $after -le $((before + 2)) ]] || {
    echo "compositor leaked fds: before=$before after=$after"
    exit 1
}
expect_alive "compositor died while cleaning unexpected fds"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
