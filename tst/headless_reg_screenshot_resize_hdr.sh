#!/usr/bin/env bash
# imway-args: --hdr 203
# The HDR editor blends its UI in a linear-light scene target sized to the
# window. Opened at ui scale 3 it is resized to 90% of the output right
# after mapping, which rebuilds the swapchain and that target with it: the
# resized window still shows the captured desktop, not a black or stale
# frame.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set display.ui_scale 3"
await 20 in_log "control: set display.ui_scale" || { echo "settings are not reachable"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/desktop.ppm"
ctl "key 99 press"; ctl "key 99 release" # Print
await 150 in_log "toplevel imway screenshot (imway-screenshot) mapped" || { echo "the editor did not open"; cat "$IMWAY_LOG"; exit 1; }

resized() {
    [[ "$(dump_field "title=imway screenshot" client_w)" == $((1280 * 9 / 10)) ]]
}
await 100 resized || { echo "the HDR editor was not resized: $(dump_field "title=imway screenshot" client_w)"; exit 1; }

drawn() {
    local x y w h
    x=$(dump_field "title=imway screenshot" imgx); y=$(dump_field "title=imway screenshot" imgy)
    w=$(dump_field "title=imway screenshot" client_w); h=$(dump_field "title=imway screenshot" client_h)
    screenshot "$XDG_RUNTIME_DIR/editor.ppm" || return 1
    # the right half of the window is the canvas showing the capture: it
    # must carry the desktop's many colours across the resized window
    python3 - "$XDG_RUNTIME_DIR/editor.ppm" "$x" "$y" "$w" "$h" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    W, H = map(int, f.readline().split())
    f.readline()
    d = f.read(W * H * 3)
x, y, w, h = map(int, sys.argv[2:6])
colours = set()
for yy in range(y + h // 4, y + h * 3 // 4, 7):
    for xx in range(x + w * 3 // 4, x + w - 4, 7):
        p = (yy * W + xx) * 3
        colours.add(d[p:p + 3])
sys.exit(0 if len(colours) > 8 else 1)
PY
}
await 50 drawn || { echo "the resized HDR editor does not show the capture"; exit 1; }

ctl "key 1 press"; ctl "key 1 release" # Escape
await 100 in_log "toplevel imway screenshot destroyed" || { echo "Escape did not close the editor"; exit 1; }
await 100 in_log "exited with status 0" || { echo "the editor did not exit cleanly"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died resizing the HDR editor"
echo "OK: the HDR editor's scene target follows a resize"
