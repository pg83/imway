#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

before=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
for _ in $(seq 1 32); do
    "$IMWAY_CLIENT"
done
after=$(find "/proc/$IMWAY_PID/fd" -mindepth 1 -maxdepth 1 | wc -l)
[[ $after -le $((before + 2)) ]] || {
    echo "compositor leaked fds: before=$before after=$after"
    exit 1
}
expect_alive "compositor died on an extra SCM_RIGHTS fd"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after extra-fd clients"
