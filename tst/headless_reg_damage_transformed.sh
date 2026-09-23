#!/usr/bin/env bash
# Surface-coordinate damage on a window whose 90-degree buffer transform is
# already in effect: the new buffer reaches the screen in full.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "red mapped"
wait_rect 'app_id=damage-transformed'
[[ "$(dump_field 'app_id=damage-transformed' client_w)" == 200 ]] || { echo "the transform did not turn the window"; dump_state; exit 1; }

shows() { # <r> <g> <b>: the window's middle shows the colour
    screenshot "$XDG_RUNTIME_DIR/_d.ppm"
    local x y
    x=$(( $(dump_field 'app_id=damage-transformed' imgx) + 100 ))
    y=$(( $(dump_field 'app_id=damage-transformed' imgy) + 60 ))
    python3 - "$XDG_RUNTIME_DIR/_d.ppm" "$x" "$y" "$@" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w * h * 3)
x, y, r, g, b = map(int, sys.argv[2:7])
ok = 0
for yy in (y - 50, y, y + 50):
    for xx in (x - 90, x, x + 90):
        i = (yy * w + xx) * 3
        ok += abs(d[i] - r) < 60 and abs(d[i + 1] - g) < 60 and abs(d[i + 2] - b) < 60
sys.exit(0 if ok == 9 else 1)
PY
}

await 50 shows 255 0 0 || { echo "the red window never showed"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-green"
wait_client "green committed"
await 50 shows 0 255 0 || { echo "the surface-damaged green buffer did not reach the whole window"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
expect_client_ok "the transformed damage client failed"
expect_alive "compositor died on surface damage under a buffer transform"
echo "OK: surface damage under a buffer transform repaints the window"
