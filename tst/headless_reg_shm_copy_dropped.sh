#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_COPY_DELAY_MS=1500 IMWAY_SHM_TRACE=1
# A subsurface drops its buffer (attaches none) while the CPU copy of the
# red one it committed is still running: the finished copy finds the
# surface without that content and leaves it be, so the red never reaches
# the screen, and the window around it is drawn as before.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
start_client
wait_client "green"

# the middle of where the subsurface sits, 100x100 at (50,50); the window
# is laid out only once its own copy landed, so its place is re-read on
# every try
middle_is() { # <r> <g> <b>: that colour, within 40 a channel
    local x y
    screenshot "$rt/frame.ppm" || return 1
    x=$(dump_field 'title=shm-copy-dropped' imgx); y=$(dump_field 'title=shm-copy-dropped' imgy)
    [[ -n "$x" && -n "$y" && "$x$y" != 00 ]] || return 1
    python3 - "$rt/frame.ppm" $((x + 100)) $((y + 100)) "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y, r, g, b = map(int, sys.argv[2:7])
p = (y * w + x) * 3
sys.exit(0 if all(abs(d[p + i] - v) <= 40 for i, v in enumerate((r, g, b))) else 1)
PY
}
await 100 middle_is 0 255 0 || { echo "the window never showed"; exit 1; }

copies() {
    grep -c "wl_shm backend cpu" "$IMWAY_LOG" || true
}
copying_after() { # <count>
    [[ "$(copies)" -gt "$1" ]]
}
n=$(copies)
touch "$rt/go-red"
wait_client "red committed"
await 50 copying_after "$n" || { echo "the red commit never reached the copy lane"; cat "$IMWAY_LOG"; exit 1; }
touch "$rt/go-drop"
wait_client "dropped"

# a screenshot waits out the copy in flight, so every frame read from here
# on comes after the red copy landed: none of them may show it
for _ in 1 2 3 4 5; do
    middle_is 0 255 0 || { echo "the dropped buffer was drawn after its copy landed"; exit 1; }
    sleep 0.2
done

touch "$rt/go-done"
expect_client_ok "the client failed"
expect_alive "compositor died when a buffer was dropped during its copy"
echo "OK: a buffer dropped during its CPU copy never reaches the screen"
