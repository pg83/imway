#!/usr/bin/env bash
# Grab popup dismissed and reopened: the second popup must map, show and
# dismiss exactly like the first.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

yellow_count() { # <ppm>
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
print(sum(1 for i in range(0, len(d), 3)
          if d[i] > 200 and d[i+1] > 200 and d[i+2] < 80))
PY
}

start_client
wait_client "ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
imgx=$(dump_field 'app_id=reopen' imgx); imgy=$(dump_field 'app_id=reopen' imgy)

round() { # <n>
    # press over the toplevel: the client opens the grab popup on this serial
    ctl "motion $((imgx + 150)) $((imgy + 100))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "motion $((imgx + 151)) $((imgy + 100))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
    ctl "button left press"
    sleep 0.1
    ctl "button left release"
    wait_client "popup$1 mapped"
    sleep 0.4

    screenshot "$XDG_RUNTIME_DIR/with$1.ppm"
    y=$(yellow_count "$XDG_RUNTIME_DIR/with$1.ppm")
    (( y > 8000 )) || { echo "popup $1 not visible (yellow=$y)"; exit 1; }

    # outside click dismisses it
    ctl "motion 1100 700"
    sleep 0.2
    ctl "button left press"
    sleep 0.1
    ctl "button left release"
    wait_client "popup$1 done"
    sleep 0.4

    screenshot "$XDG_RUNTIME_DIR/without$1.ppm"
    y=$(yellow_count "$XDG_RUNTIME_DIR/without$1.ppm")
    (( y < 100 )) || { echo "popup $1 did not disappear (yellow=$y)"; exit 1; }
}

round 1
round 2

expect_client_ok "popup reopen broke"
expect_alive "compositor died reopening a grab popup"
echo "OK: grab popup dismissed and reopened identically twice"
