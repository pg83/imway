#!/usr/bin/env bash
# A commit timed for a moment past what 64-bit nanoseconds hold is held for
# good, not wrapped round to a past time and shown at once: over a run of
# presented frames the window stays red and the commit's frame callback
# never comes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "committed"
wait_rect 'app_id=commit-timing-never'

red() {
    local x y
    x=$(( $(dump_field 'app_id=commit-timing-never' imgx) + 150 ))
    y=$(( $(dump_field 'app_id=commit-timing-never' imgy) + 150 ))
    python3 - "$1" "$x" "$y" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w * h * 3)
x, y = int(sys.argv[2]), int(sys.argv[3])
i = (y * w + x) * 3
sys.exit(0 if d[i] > 200 and d[i + 1] < 60 else 1)
PY
}

for i in $(seq 1 10); do
    screenshot "$XDG_RUNTIME_DIR/_t.ppm"
    red "$XDG_RUNTIME_DIR/_t.ppm" || { echo "the commit timed for never was shown (frame $i)"; exit 1; }
done

touch "$XDG_RUNTIME_DIR/go-exit"
expect_client_ok "the commit timed for never was presented"
expect_alive "compositor died holding a commit timed for never"
echo "OK: a timestamp past 64-bit nanoseconds holds the commit for good"
