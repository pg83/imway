#!/usr/bin/env bash
# A client's own cursor surface rides the hardware cursor plane: with the
# plane off it is composited into the frame, with the plane on it leaves the
# frame, and a null cursor hides the plane without the session missing a
# flip.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "cursor plane 105" || { echo "no hardware cursor at boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_mapped
cursor_set() {
    grep -q "cursor-set" "$CLIENT_LOG"
}
for _ in $(seq 1 20); do
    point_at_color 0 255 0 || { echo "client window not found"; exit 1; }
    ctl "relmotion 1 1"
    await 10 cursor_set && break
done
wait_client "cursor-set"

stripes() { # prints the number of cursor stripe pixels in a fresh frame
    screenshot "$XDG_RUNTIME_DIR/frame.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/frame.ppm" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
print(sum(1 for i in range(0, len(d), 3)
          if (d[i] > 150 and d[i + 1] < 60 and d[i + 2] < 60) or
             (d[i + 2] > 150 and d[i] < 60 and d[i + 1] < 60)))
PY
}
composited() {
    [[ "$(stripes)" -gt 500 ]]
}
on_plane() {
    [[ "$(stripes)" -lt 50 ]]
}

ctl "set advanced.hardware_cursor false"
await 50 composited || { echo "the client cursor surface never reached the frame"; exit 1; }

ctl "set advanced.hardware_cursor true"
await 50 on_plane || { echo "the client cursor surface stayed in the frame with the plane on"; exit 1; }
[[ "$(dump_field '^cursor' surface)" == 1 ]] || { echo "the client cursor surface is gone"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
ctl "key 30 press"; ctl "key 30 release" # KEY_A: the client hides its cursor
wait_client "cursor-hidden"
hidden() {
    [[ "$(dump_field '^cursor' surface)" == 0 ]]
}
await 100 hidden || { echo "the null cursor did not replace the surface"; dump_state; exit 1; }
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "motion 20 20"
await 100 advanced || { echo "flips stopped after hiding the cursor"; exit 1; }

expect_alive "compositor died moving a client cursor to the plane"
echo "OK: a client cursor surface goes to the hardware plane and hides there"
