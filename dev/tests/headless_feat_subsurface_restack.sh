#!/usr/bin/env bash
# subsurface place_below: moving B under A grows visible green, shrinks blue.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

count() { # <ppm> -> "green blue"
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
g = sum(1 for i in range(0, len(d), 3) if d[i] < 80 and d[i+1] > 200 and d[i+2] < 80)
b = sum(1 for i in range(0, len(d), 3) if d[i] < 80 and d[i+1] < 80 and d[i+2] > 200)
print(g, b)
PY
}

start_client
wait_client "state1"
screenshot "$XDG_RUNTIME_DIR/s1.ppm"
read -r g1 b1 < <(count "$XDG_RUNTIME_DIR/s1.ppm")

wait_client "state2"
screenshot "$XDG_RUNTIME_DIR/s2.ppm"
read -r g2 b2 < <(count "$XDG_RUNTIME_DIR/s2.ppm")

echo "state1 green=$g1 blue=$b1 → state2 green=$g2 blue=$b2"
[[ "$g2" -gt "$g1" && "$b2" -lt "$b1" ]] || { echo "place_below did not restack the subsurfaces"; exit 1; }
echo "OK: subsurface place_below restacked (green uncovered, blue occluded)"
