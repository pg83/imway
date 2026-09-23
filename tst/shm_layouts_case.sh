# Eight sealed wl_shm buffers laid out as a zero-copy import cannot take them
# (client_reg_shm_layouts: odd stride, odd offset, an offset off the page,
# a pool that is not whole pages, two buffers sharing a pool, and the odd
# stride and offset again in pools of whole pages), on the backend the
# scenario forces: each falls back to the CPU copy or to what the device
# can do, and each is drawn with its rows where they belong, its colour in
# the top half and white in the bottom one.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_layouts"
start_client
wait_client "layouts committed"
wait_rect 'title=shm-layouts'

cells_right() {
    local x y
    x=$(dump_field 'title=shm-layouts' imgx); y=$(dump_field 'title=shm-layouts' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/layouts.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/layouts.ppm" "$x" "$y" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0 = map(int, sys.argv[2:4])
tops = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (255, 128, 0), (128, 0, 255)]
bad = []
for i, top in enumerate(tops):
    for yy, want in ((20 + 8, top), (20 + 22, top), (20 + 38, (255, 255, 255)), (20 + 52, (255, 255, 255))):
        for xx in (10 + i * 80 + 5, 10 + i * 80 + 30, 10 + i * 80 + 54):
            p = ((y0 + yy) * w + x0 + xx) * 3
            got = tuple(d[p:p + 3])
            if any(abs(g - c) > 40 for g, c in zip(got, want)):
                bad.append((i, xx, yy, got, want))
if bad:
    print("misdrawn:", bad[:6])
    sys.exit(1)
PY
}
await 100 cells_right || { echo "a buffer was not drawn as laid out"; cells_right; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on unusual wl_shm layouts"
echo "OK: every unusual wl_shm layout is drawn as laid out"
