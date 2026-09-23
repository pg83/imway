#!/usr/bin/env bash
# A popup is drawn on the overlay path, apart from its window's surface
# tree, and must honour the same buffer transforms and viewport as the
# window's own surfaces (client_reg_overlay_transforms): for each of the
# four transforms that swap width and height, and for a viewport
# destination that scales the buffer, the popup's four coloured quadrants
# land in the same corners and span the same box as a reference subsurface
# of the window showing the same buffer. Subsurfaces without a buffer, on
# either, draw nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
start_client
wait_client "phase 0"
wait_rect 'title=overlay-transforms'
wait_placed 'title=overlay-transforms' || { echo "the window never settled"; exit 1; }
# the pointer stays clear of both: a composited cursor would cover a quadrant
ctl "motion 1200 780"

layouts() { # echo the reference's and the popup's layout: quadrant of each colour and the box size
    local x y
    x=$(dump_field 'title=overlay-transforms' imgx); y=$(dump_field 'title=overlay-transforms' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$rt/shot.ppm" || return 1
    python3 - "$rt/shot.ppm" "$x" "$y" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    W, H = map(int, f.readline().split())
    f.readline()
    d = f.read(W * H * 3)
x0, y0 = int(sys.argv[2]), int(sys.argv[3])
colours = {'red': (255, 0, 0), 'blue': (0, 0, 255), 'green': (0, 255, 0), 'yellow': (255, 255, 0)}

def layout(rx0, rx1):
    pts = {k: [] for k in colours}
    for yy in range(max(0, y0), min(H, y0 + 260)):
        for xx in range(max(0, rx0), min(W, rx1)):
            p = (yy * W + xx) * 3
            for k, c in colours.items():
                if all(abs(d[p + i] - c[i]) <= 30 for i in range(3)):
                    pts[k].append((xx, yy))
    if not all(pts.values()):
        return 'missing'
    allx = [x for v in pts.values() for x, _ in v]
    ally = [y for v in pts.values() for _, y in v]
    cx, cy = (min(allx) + max(allx)) / 2, (min(ally) + max(ally)) / 2
    quads = []
    for k in colours:
        mx = sum(x for x, _ in pts[k]) / len(pts[k])
        my = sum(y for _, y in pts[k]) / len(pts[k])
        quads.append(k + ':' + ('b' if my > cy else 't') + ('r' if mx > cx else 'l'))
    return ','.join(quads) + f' {max(allx) - min(allx) + 1}x{max(ally) - min(ally) + 1}'

print(layout(x0, x0 + 200) + ' | ' + layout(x0 + 200, x0 + 480))
PY
}
alike() { # <phase> <box WxH>: the popup matches the reference, which spans the box
    local l ref pop
    l=$(layouts) || return 1
    echo "$l" > "$rt/layout"
    ref=${l%% | *}; pop=${l##* | }
    [[ "$ref" != missing && "$ref" == "$pop" && "${ref##* }" == "$2" ]]
}

for phase in 0 1 2 3 4; do
    wait_client "phase $phase"
    box=40x80
    [[ $phase == 4 ]] && box=160x80
    await 50 alike "$phase" "$box" || {
        echo "phase $phase: the popup does not match the reference: $(cat "$rt/layout" 2>/dev/null) (want $box)"
        exit 1
    }
    echo "phase $phase: $(cat "$rt/layout")"
    touch "$rt/go-$phase"
done

wait_client "overlay transforms done"
expect_client_ok "the overlay transforms client failed"
expect_alive "compositor died drawing transformed popups"
echo "OK: popups honour buffer transforms and viewports like the window's own surfaces"
