# A client with 1200 textured subsurfaces, more than the renderer's first
# descriptor pool of 1024 sets holds, so the texture pool grows a second
# pool while the flood is uploaded. $missing lists how many cells the
# scenario may lose to its IMWAY_CHAOS fault on the first upload; a cell that
# lost its descriptor stays blank until its next commit, when every cell must
# be drawn again (the client recommits all of them in blue on KEY_A).
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_texture_flood"
start_client
wait_client "flood committed 1200"
wait_rect 'title=texture-flood'

cells() { # <r> <g> <b>: how many of the 1200 cells show that colour
    local x y
    x=$(dump_field 'title=texture-flood' imgx)
    y=$(dump_field 'title=texture-flood' imgy)
    screenshot "$XDG_RUNTIME_DIR/flood.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/flood.ppm" "$x" "$y" "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0, r, g, b = map(int, sys.argv[2:7])
n = 0
for i in range(1200):
    cx = x0 + (i % 40) * 10 + 5
    cy = y0 + (i // 40) * 10 + 5
    p = (cy * w + cx) * 3
    if all(abs(d[p + c] - v) <= 40 for c, v in enumerate((r, g, b))):
        n += 1
print(n)
PY
}
drawn() { # <r> <g> <b> <count>
    [[ "$(cells "$1" "$2" "$3")" == "$4" ]]
}
# the green count once uploads have stopped changing it: the CPU copies of
# 1200 cells land over several frames, and a software rasterizer takes its
# time over each, so a count is only final once three reads agree
settled_green() {
    local a b c i
    a=$(cells 0 255 0); b=-1; c=-2
    for i in $(seq 1 60); do
        sleep 0.3
        c=$b; b=$a; a=$(cells 0 255 0)
        [[ "$a" == "$b" && "$b" == "$c" && "$a" -gt 0 ]] && { echo "$a"; return 0; }
    done
    echo "$a"
    return 1
}

green=$(settled_green) || { echo "the flood never settled: $green green cells"; cat "$IMWAY_LOG"; exit 1; }
ok=0
for m in $missing; do
    [[ "$green" == "$((1200 - m))" ]] && ok=1
done
[[ $ok == 1 ]] || { echo "saw $green green cells, expected 1200 less one of: $missing"; cat "$IMWAY_LOG"; exit 1; }
echo "green cells after the flood: $green"

ctl "key 30 press"; ctl "key 30 release" # KEY_A: recommit every cell
wait_client "flood recommitted"
await 100 drawn 0 0 255 1200 || { echo "expected all 1200 cells blue after the recommit, saw $(cells 0 0 255)"; exit 1; }

expect_alive "compositor died on a texture flood"
echo "OK: 1200 textured subsurfaces, $((1200 - green)) lost to the fault and back on the next commit"
