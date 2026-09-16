#!/usr/bin/env bash
# buffer_transform 180: 200x120 stays 200x120, but red|blue becomes blue|red.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped

# one completed dump round trip proves the compositor has drained the
# client's socket, so the next composed frame carries the surface; the
# retries absorb a slow first frame
dump_state >/dev/null

shot_ok() {
    screenshot "$XDG_RUNTIME_DIR/shot.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
red = [(x, y) for y in range(h) for x in range(w)
       if d[(y*w+x)*3] > 200 and d[(y*w+x)*3+1] < 60 and d[(y*w+x)*3+2] < 60]
blue = [(x, y) for y in range(h) for x in range(w)
        if d[(y*w+x)*3+2] > 200 and d[(y*w+x)*3] < 60 and d[(y*w+x)*3+1] < 60]
assert red and blue, "two-tone surface not visible"
allpts = red + blue
bw = max(x for x, _ in allpts) - min(x for x, _ in allpts) + 1
bh = max(y for _, y in allpts) - min(y for _, y in allpts) + 1
# after 180 the left half is now blue and the right half is now red
rx = sum(x for x, _ in red) / len(red)
bx = sum(x for x, _ in blue) / len(blue)
print(f"bbox={bw}x{bh}, red_cx={rx:.0f} blue_cx={bx:.0f}")
assert 190 <= bw <= 210 and 110 <= bh <= 130, "180 must not swap dimensions"
assert rx > bx, "colors were not flipped left<->right by the 180 transform"
PY
}

shot_ok || shot_ok || shot_ok || { echo "the surface never reached the screen as expected"; exit 1; }
echo "OK: buffer_transform 180 flipped the colors, kept dimensions"
