#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=cpu IMWAY_SHM_COPY_DELAY_MS=3000 IMWAY_SHM_TRACE=1 IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1
# Reading the screen while a wl_shm commit is still on the CPU copy lane
# (held there for three seconds): a screenshot taken right after a window
# turns blue waits the copy out and shows the new colour, never the
# window's previous content. So does an eyedropper pick right after it
# turns red: with the cursor composited (as on KMS without a cursor plane)
# the pick composes a frame of its own without the cursor, and that frame
# carries the red commit.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rt="$XDG_RUNTIME_DIR"
start_client
wait_client "green"
wait_rect 'title=shm-copy-race'
cx=$(($(dump_field 'title=shm-copy-race' imgx) + 200))
cy=$(($(dump_field 'title=shm-copy-race' imgy) + 150))

copies() {
    grep -c "wl_shm backend cpu" "$IMWAY_LOG" || true
}
copying_after() { # <count>: a later commit has gone to the copy lane
    [[ "$(copies)" -gt "$1" ]]
}
pixel() { # <ppm> <x> <y>: "r g b"
    python3 - "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y = int(sys.argv[2]), int(sys.argv[3])
p = (y * w + x) * 3
print(d[p], d[p + 1], d[p + 2])
PY
}
is_color() { # <"r g b"> <r> <g> <b>
    local r g b
    read -r r g b <<<"$1"
    ((r >= $2 - 40 && r <= $2 + 40 && g >= $3 - 40 && g <= $3 + 40 && b >= $4 - 40 && b <= $4 + 40))
}

# the screenshot command, straight after the blue commit went to the copy
n=$(copies)
touch "$rt/go-blue"
wait_client "blue"
await 50 copying_after "$n" || { echo "the blue commit never reached the copy lane"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$rt/blue.ppm"
got=$(pixel "$rt/blue.ppm" "$cx" "$cy")
is_color "$got" 0 0 255 || { echo "the screenshot taken during the copy shows $got, not the blue commit"; exit 1; }

# the eyedropper, armed from the launcher, picks straight after the red
# commit went to the copy
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher never took text"; dump_state; exit 1; }
ctl "type color picker"
await_input "color picker" || { echo "the field did not take 'color picker'"; dump_state; exit 1; }
ctl "key 103 press"; ctl "key 103 release" # Up: into the action row
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "the launcher did not close"; dump_state; exit 1; }

n=$(copies)
touch "$rt/go-red"
wait_client "red"
await 50 copying_after "$n" || { echo "the red commit never reached the copy lane"; cat "$IMWAY_LOG"; exit 1; }
ctl "motion $cx $cy"
ctl "button left press"; ctl "button left release"

swatch_up() {
    [[ -n "$(dump_field '^imgui name=##pick' x)" ]]
}
await 100 swatch_up || { echo "the picker swatch did not appear"; dump_state; exit 1; }
sx=$(($(dump_field '^imgui name=##pick' x) + 20))
sy=$(($(dump_field '^imgui name=##pick' y) + 20))
screenshot "$rt/pick.ppm"
got=$(pixel "$rt/pick.ppm" "$sx" "$sy")
is_color "$got" 255 0 0 || { echo "the pick during the copy shows $got, not the red commit"; exit 1; }

ctl "key 1 press"; ctl "key 1 release" # Escape closes the swatch
swatch_gone() {
    ! swatch_up
}
await 50 swatch_gone || { echo "Escape did not close the swatch"; dump_state; exit 1; }

touch "$rt/go-done"
expect_client_ok "the recoloured client failed"
expect_alive "compositor died reading the screen during a wl_shm copy"
echo "OK: a screenshot and a pick taken during a wl_shm copy carry the new commit"
