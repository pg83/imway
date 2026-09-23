#!/usr/bin/env bash
# The window shadow's strength setting takes any number but draws within
# 0..2: a strength past 2 darkens exactly as much as 2, a negative one draws
# no more shadow than shadows switched off, and in between the band under a
# window darkens with the strength.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_render_fault"
start_client
wait_client "render fault ready"
wait_rect 'title=render-fault-victim'

band() { # <name>: mean brightness of a band just under the window, settled
    local shot="$XDG_RUNTIME_DIR/$1.ppm" i rect prev_rect="" mean prev=""
    # a loaded runner may still be placing the window: the rect is re-read
    # for every try, and the band counts once two frames in a row at the
    # same rect agree. A setting sent before this is in every one of them:
    # the control FIFO runs it before the screenshot that follows it
    for i in $(seq 1 40); do
        kill -0 "$CLIENT_PID" 2>/dev/null || { echo "the window's client is gone" >&2; return 1; }
        rect=$(dump_state | awk '/title=render-fault-victim/ { for (i = 1; i <= NF; i++) { split($i, kv, "="); f[kv[1]] = kv[2] } print f["x"], f["y"], f["w"], f["h"]; exit }')
        read -r x y w h <<<"$rect"
        if [[ -z "$h" ]] || ! screenshot "$shot"; then
            sleep 0.1
            continue
        fi
        mean=$(python3 - "$shot" "$x" "$((y + h + 2))" "$((x + w))" "$((y + h + 12))" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0, x1, y1 = map(int, sys.argv[2:6])
print(sum(sum(d[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3]) for yy in range(y0, y1) for xx in range(x0, x1)) / ((y1 - y0) * (x1 - x0) * 3))
PY
)
        if [[ "$rect" == "$prev_rect" && -n "$prev" ]] && python3 -c 'import sys; sys.exit(abs(float(sys.argv[1]) - float(sys.argv[2])) > .05)' "$mean" "$prev"; then
            printf '%.3f\n' "$mean"
            return 0
        fi
        prev=$mean
        prev_rect=$rect
    done
    echo "the band under the window never settled for $1" >&2
    return 1
}

ctl "set appearance.window_shadows false"
off=$(band off)
ctl "set appearance.window_shadows true"
ctl "set appearance.shadow_strength 1";  one=$(band one)
ctl "set appearance.shadow_strength 2";  two=$(band two)
ctl "set appearance.shadow_strength 5";  five=$(band five)
ctl "set appearance.shadow_strength -1"; negative=$(band negative)
echo "band: off=$off 1=$one 2=$two 5=$five -1=$negative"

python3 - "$off" "$one" "$two" "$five" "$negative" <<'PY'
import sys
off, one, two, five, negative = map(float, sys.argv[1:])
assert one < off - 1, "strength 1 casts no shadow under the window"
assert two < one - 1, "strength 2 is not darker than 1"
assert abs(five - two) < .5, "a strength past 2 is not held at 2"
assert abs(negative - off) < .5, "a negative strength still casts a shadow"
PY

expect_alive "compositor died drawing window shadows"
echo "OK: the shadow strength draws within 0..2"
