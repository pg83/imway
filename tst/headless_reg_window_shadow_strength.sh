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

x=$(dump_field 'title=render-fault-victim' x); y=$(dump_field 'title=render-fault-victim' y)
w=$(dump_field 'title=render-fault-victim' w); h=$(dump_field 'title=render-fault-victim' h)

band() { # <name>: mean brightness of a band just under the window, settled
    local a="$XDG_RUNTIME_DIR/$1.a.ppm" b="$XDG_RUNTIME_DIR/$1.b.ppm" i
    for i in $(seq 1 30); do
        screenshot "$a" && screenshot "$b" || continue
        python3 - "$a" "$b" "$x" "$((y + h + 2))" "$((x + w))" "$((y + h + 12))" <<'PY' && return 0
import sys
def load(p):
    with open(p, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, f.read(w * h * 3)
w, a = load(sys.argv[1])
_, b = load(sys.argv[2])
x0, y0, x1, y1 = map(int, sys.argv[3:7])
def mean(d):
    return sum(sum(d[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3]) for yy in range(y0, y1) for xx in range(x0, x1)) / ((y1 - y0) * (x1 - x0) * 3)
ma, mb = mean(a), mean(b)
if abs(ma - mb) > .05:
    sys.exit(1)
print(f"{ma:.3f}")
PY
    done
    echo "the band under the window never settled for $1" >&2
    return 1
}
strength() { # <value>
    ctl "set appearance.shadow_strength $1"
    await 20 in_log "control: set appearance.shadow_strength" || return 1
    sleep 0.2
}

ctl "set appearance.window_shadows false"
sleep 0.2
off=$(band off)
ctl "set appearance.window_shadows true"
strength 1;  one=$(band one)
strength 2;  two=$(band two)
strength 5;  five=$(band five)
strength -1; negative=$(band negative)
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
