# A client with 1200 textured subsurfaces, more than the renderer's first
# descriptor pool of 1024 sets holds, so the texture pool grows a second
# pool while the flood is uploaded. $missing is how many cells the scenario
# expects to lose to its IMWAY_CHAOS fault on the first upload; a cell that
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

await 100 drawn 0 255 0 "$((1200 - missing))" || {
    echo "expected $((1200 - missing)) green cells, saw $(cells 0 255 0)"
    cat "$IMWAY_LOG"
    exit 1
}
# the blank cells stay blank: nothing retries them before their next commit
sleep 0.3
drawn 0 255 0 "$((1200 - missing))" || { echo "the cell count changed without a commit: $(cells 0 255 0)"; exit 1; }

ctl "key 30 press"; ctl "key 30 release" # KEY_A: recommit every cell
wait_client "flood recommitted"
await 100 drawn 0 0 255 1200 || { echo "expected all 1200 cells blue after the recommit, saw $(cells 0 0 255)"; exit 1; }

expect_alive "compositor died on a texture flood"
echo "OK: 1200 textured subsurfaces, $missing lost to the fault and back on the next commit"
