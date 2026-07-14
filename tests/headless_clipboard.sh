#!/usr/bin/env bash
# Интеграционный тест: clipboard + primary_selection через wl-copy/wl-paste.
set -euo pipefail

IMWAY="$1"

command -v wl-copy >/dev/null || { echo "нет wl-clipboard"; exit 127; }

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"

"$IMWAY" --socket imway-test --frames 400 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -S "$RT/imway-test" ]] && break
    sleep 0.1
done
[[ -S "$RT/imway-test" ]] || { echo "сокет не появился"; exit 1; }

export WAYLAND_DISPLAY=imway-test

echo -n "clipboard payload" | timeout 5 wl-copy &
sleep 0.7
GOT="$(timeout 5 wl-paste)"
[[ "$GOT" == "clipboard payload" ]] || { echo "clipboard: получено '$GOT'"; exit 1; }

echo -n "primary payload" | timeout 5 wl-copy --primary &
sleep 0.7
GOT="$(timeout 5 wl-paste --primary)"
[[ "$GOT" == "primary payload" ]] || { echo "primary: получено '$GOT'"; exit 1; }

kill "$IMWAY_PID" 2>/dev/null || true
echo ok
