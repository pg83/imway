#!/usr/bin/env bash
# M2-тест: печать в foot через control-канал создаёт файл.
# Цепочка: FIFO → seat → wl_keyboard → foot → pty → shell → touch.
set -euo pipefail

IMWAY="$1"

command -v foot >/dev/null || { echo "SKIP: нет foot"; exit 127; }

RT="$(mktemp -d)"
trap 'rm -rf "$RT"' EXIT
export XDG_RUNTIME_DIR="$RT"
CTL="$RT/ctl"
MARKER="$RT/m2ok"

"$IMWAY" --socket imway-input --control "$CTL" --frames 1200 \
    --screenshot "$RT/shot.ppm" >"$RT/imway.log" 2>&1 &
IMWAY_PID=$!

for _ in $(seq 1 50); do
    [[ -p "$CTL" && -S "$RT/imway-input" ]] && break
    sleep 0.1
done

WAYLAND_DISPLAY=imway-input foot >"$RT/foot.log" 2>&1 &
FOOT_PID=$!

# ждём map
for _ in $(seq 1 100); do
    grep -q "mapped" "$RT/imway.log" && break
    sleep 0.1
done
grep -q "mapped" "$RT/imway.log" || { echo "foot не замапился"; cat "$RT/foot.log"; exit 1; }
sleep 1 # дать шеллу внутри foot прогрузиться

{
    echo "type touch $MARKER"
    sleep 0.3
    echo "key 28 press"   # Enter
    echo "key 28 release"
} >"$CTL"

for _ in $(seq 1 100); do
    [[ -e "$MARKER" ]] && break
    sleep 0.1
done

echo "quit" >"$CTL" || true
wait "$IMWAY_PID" || true
kill "$FOOT_PID" 2>/dev/null || true

[[ -e "$MARKER" ]] || {
    echo "FAIL: набранная команда не исполнилась (нет $MARKER)"
    tail -5 "$RT/imway.log" "$RT/foot.log"
    exit 1
}
echo "OK: клавиатурный ввод дошёл до шелла внутри foot"
