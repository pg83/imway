#!/usr/bin/env bash
# imway-args: --hdr 203
# The HDR screenshot viewer's swapchain going stale under it: the viewer
# blends its UI in a linear-light scene target sized to the window, so a
# rebuild after an acquire or a present reports the swapchain out of date
# (the viewer's IMWAY_CHAOS) waits for the GPU to let go of that target and
# builds it again with the swapchain; the editor is drawn and closes
# cleanly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
drawn() { # the editor's panel and canvas are on screen: many colours in its rect
    local x y w h
    x=$(dump_field 'title=imway screenshot' imgx); y=$(dump_field 'title=imway screenshot' imgy)
    w=$(dump_field 'title=imway screenshot' client_w); h=$(dump_field 'title=imway screenshot' client_h)
    [[ -n "$x" && -n "$h" ]] || return 1
    screenshot "$rt/editor.ppm" || return 1
    python3 - "$rt/editor.ppm" "$x" "$y" "$w" "$h" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    W, H = map(int, f.readline().split())
    f.readline()
    d = f.read(W * H * 3)
x, y, w, h = map(int, sys.argv[2:6])
colours = {d[((y + yy) * W + x + xx) * 3:((y + yy) * W + x + xx) * 3 + 3] for yy in range(2, h - 2, 5) for xx in range(2, w - 2, 5)}
sys.exit(0 if len(colours) > 4 else 1)
PY
}
stale() { # <fault>
    env IMWAY_SHOT_COLOR=1:203 IMWAY_CHAOS="$1" "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 &
    local pid=$!
    await 150 viewer_up || { echo "$1: the HDR editor did not open"; cat "$rt/viewer.out"; exit 1; }
    await 50 drawn || { echo "$1: the HDR editor was not drawn after the rebuild"; exit 1; }
    escape_until viewer_gone || { echo "$1: Escape did not close the HDR editor"; exit 1; }
    local rc=0
    wait "$pid" || rc=$?
    [[ $rc -eq 0 ]] || { echo "$1: the HDR viewer exited $rc"; cat "$rt/viewer.out"; exit 1; }
}

stale swapchain=0            # the first acquire
stale swapchain=3            # a present
stale swapchain-suboptimal=3 # a present, suboptimal

expect_alive "compositor died while the HDR viewer rebuilt its swapchain"
echo "OK: a stale swapchain under the HDR viewer is rebuilt with its scene target"
