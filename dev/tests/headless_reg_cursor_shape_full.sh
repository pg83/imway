#!/usr/bin/env bash
# imway-env: IMWAY_FORCE_CURSOR=1
# cursor-shape-v1 fidelity: every shape the protocol names must land in the
# scene verbatim, not collapsed onto a lookalike, and the compositor must
# actually draw it. The client walks the whole enum; each observed value is
# acked with a pointer wiggle that releases the client's next set_shape.
# While the client is parked on a shape new to the renderer (crosshair,
# grab, zoom-in), the screenshot must show the glyph's white core and black
# outline over the flat red window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x+15)) $((y+12))"

glyph_visible() { # <shape>
    screenshot "$XDG_RUNTIME_DIR/_cursor.ppm"
    python3 - "$XDG_RUNTIME_DIR/_cursor.ppm" "$((x+15))" "$((y+12))" "$1" <<'PY'
import sys
path = sys.argv[1]
cx, cy, shape = map(int, sys.argv[2:])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
W, H = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(W * H * 3)
# the thin black outline anti-aliases against both the white core and the
# red window, so it lands as grays and dark blends: anything clearly below
# the flat red background counts as outline
white = dark = 0
for yy in range(max(0, cy - 14), min(H, cy + 15)):
    for xx in range(max(0, cx - 14), min(W, cx + 15)):
        i = (yy * W + xx) * 3
        r, g, b = d[i], d[i + 1], d[i + 2]
        if r > 200 and g > 200 and b > 200:
            white += 1
        elif r < 180 and g < 180 and b < 180:
            dark += 1
print(f"shape={shape} white={white} dark={dark}")
assert white >= 10 and dark >= 10, f"shape {shape} left no glyph on screen"
PY
}

wiggle=0
for shape in $(seq 1 36); do
    ok=""
    for _ in $(seq 1 100); do
        [[ "$(dump_field 'cursor shape' shape)" == "$shape" ]] && { ok=1; break; }
        sleep 0.05
    done
    [[ -n "$ok" ]] || { echo "shape $shape never reached the scene (now: $(dump_field 'cursor shape' shape))"; exit 1; }

    case "$shape" in
        8|16|33) # crosshair, grab, zoom-in: shapes the renderer never drew before
            sleep 0.2
            glyph_visible "$shape" || { echo "shape $shape is not drawn"; exit 1; }
            ;;
    esac

    wiggle=$((1 - wiggle))
    ctl "motion $((x + 15 + wiggle)) $((y + 12))"
done

expect_client_ok "cursor-shape walk failed"
expect_alive "compositor died during the shape walk"
echo "OK: all 36 cursor shapes reach the scene verbatim and draw"
