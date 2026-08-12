#!/usr/bin/env bash
# color-management-v1: the manager advertises capabilities, and a PQ+BT.2020
# surface is converted — its composited color changes vs the raw bytes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# sample the mean color of the surface's raw fill rgb(180,120,60) in a ppm
sample() { # <ppm> -> "R G B" mean over pixels near the target (or the whole surface region)
    python3 - "$@" <<'PY'
import sys
path = sys.argv[1]
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
# the raw fill is rgb(180,120,60); find those pixels (loose match) and average
xs = []
for i in range(0, len(d), 3):
    r, g, b = d[i], d[i+1], d[i+2]
    if abs(r-180) < 60 and abs(g-120) < 60 and abs(b-60) < 60:
        xs.append((r, g, b))
if not xs:
    print("0 0 0 0"); sys.exit(0)
n = len(xs)
print(sum(p[0] for p in xs)//n, sum(p[1] for p in xs)//n, sum(p[2] for p in xs)//n, n)
PY
}

start_client
wait_client "raw"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/raw.ppm"
read -r r1 g1 b1 n1 < <(sample "$XDG_RUNTIME_DIR/raw.ppm")
[[ "$n1" -gt 5000 ]] || { echo "raw surface not found (n=$n1)"; cat "$CLIENT_LOG"; exit 1; }

wait_client "managed"
# the conversion needs a rendered frame; poll until the surface region changes
changed=0
for _ in $(seq 1 20); do
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/managed.ppm"
    changed=$(region_diff "$XDG_RUNTIME_DIR/raw.ppm" "$XDG_RUNTIME_DIR/managed.ppm" 0 0 1280 800)
    [[ "$changed" -gt 3000 ]] && break
done

echo "raw rgb=($r1,$g1,$b1) n=$n1; changed pixels=$changed"
[[ "$changed" -gt 3000 ]] || { echo "PQ+BT.2020 surface was NOT converted (composited raw)"; cat "$CLIENT_LOG"; exit 1; }
echo "OK: color-management handshake + PQ/BT.2020 surface converted"
