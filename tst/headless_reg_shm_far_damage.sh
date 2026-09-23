#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu
# A new wl_shm buffer whose only damage lies outside it: the damage clips to
# nothing, and the CPU copy takes the whole new buffer rather than none of
# it, so the new colour is on screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "mapped"
wait_rect 'title=far-damage'

colour_at_centre() { # <r> <g> <b>
    local x y
    x=$(dump_field 'title=far-damage' imgx); y=$(dump_field 'title=far-damage' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/far.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/far.ppm" "$((x + 100))" "$((y + 75))" "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y, r, g, b = map(int, sys.argv[2:7])
p = (y * w + x) * 3
sys.exit(0 if all(abs(d[p + i] - v) <= 40 for i, v in enumerate((r, g, b))) else 1)
PY
}

await 100 colour_at_centre 255 0 0 || { echo "the red buffer was not drawn"; exit 1; }
ctl "key 30 press"; ctl "key 30 release" # KEY_A
wait_client "blue committed"
await 100 colour_at_centre 0 0 255 || { echo "the buffer damaged only outside itself was not drawn"; exit 1; }

expect_alive "compositor died on damage outside the buffer"
echo "OK: damage outside the buffer still brings the new content on screen"
