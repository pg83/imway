#!/usr/bin/env bash
# Устойчивость: клиент обрезает shm-пул под ногами композитора; композитор обязан выжить.
set -euo pipefail

IMWAY="$1"
CLIENT="$2"

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"

"$IMWAY" --socket imway-test --frames 200 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "сокет не появился"; exit 1; }

WAYLAND_DISPLAY=imway-test "$CLIENT" || true

wait "$IMWAY_PID"
RC=$?
[[ $RC -eq 0 ]] || { echo "композитор умер (rc=$RC)"; exit 1; }
echo ok
