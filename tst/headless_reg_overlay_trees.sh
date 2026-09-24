#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1
# Surface trees drawn beyond the plain case (client_reg_overlay_trees):
# a toplevel's subsurface stacked below it shows only through the
# toplevel's transparent half; a popup is drawn as its window geometry, a
# 100x80 crop of its 120x100 buffer, with a subsurface above it inside and
# one below it showing only past its left edge; and a cursor surface whose
# viewport crops and scales it is composited at the viewport's 24x24.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "trees mapped"
wait_rect 'title=overlay-trees'
# aim at where the window settled: a pointer aimed at a first placement
# lands on the popup, and the cursor then covers its green
wait_placed 'title=overlay-trees' || { echo "the window never settled"; exit 1; }

x=$(dump_field 'title=overlay-trees' imgx); y=$(dump_field 'title=overlay-trees' imgy)
# enter the blue half so the client sets its cursor there
for _ in $(seq 1 20); do
    ctl "motion $((x + 250)) $((y + 170))"
    sleep 0.1
    ctl "motion $((x + 251)) $((y + 170))"
    grep -q "cursor-set" "$CLIENT_LOG" && break
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
done
wait_client "cursor-set"

counts() {
    screenshot "$XDG_RUNTIME_DIR/trees.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/trees.ppm" "$x" "$y" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0 = map(int, sys.argv[2:4])
colours = {'red': (255, 0, 0), 'green': (0, 255, 0), 'yellow': (255, 255, 0), 'magenta': (255, 0, 255), 'cyan': (0, 255, 255)}
n = dict.fromkeys(colours, 0)
for yy in range(max(0, y0 - 40), min(h, y0 + 360)):
    for xx in range(max(0, x0 - 40), min(w, x0 + 440)):
        p = (yy * w + xx) * 3
        for name, c in colours.items():
            if all(abs(d[p + i] - c[i]) <= 30 for i in range(3)):
                n[name] += 1
print(' '.join(f"{k}={v}" for k, v in n.items()))
PY
}
expected() {
    local c
    c=$(counts) || return 1
    echo "$c" > "$XDG_RUNTIME_DIR/counts"
    python3 - $c <<'PY'
import sys
n = dict(kv.split('=') for kv in sys.argv[1:])
n = {k: int(v) for k, v in n.items()}
ok = (abs(n['red'] - 800) <= 60 and abs(n['green'] - 7600) <= 300 and abs(n['yellow'] - 400) <= 40
      and abs(n['magenta'] - 750) <= 60 and abs(n['cyan'] - 576) <= 60)
sys.exit(0 if ok else 1)
PY
}
await 50 expected || {
    echo "unexpected colour areas: $(cat "$XDG_RUNTIME_DIR/counts" 2>/dev/null)"
    echo "want red=800 green=7600 yellow=400 magenta=750 cyan=576"
    exit 1
}
echo "areas: $(cat "$XDG_RUNTIME_DIR/counts")"

expect_alive "compositor died drawing the overlay trees"
echo "OK: below-stacked subsurfaces, cropped popups and viewported cursors draw as they should"
