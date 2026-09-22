#!/usr/bin/env bash
# Surface damage that overflows once scaled to the buffer, and damage on a
# surface with a viewport, both stand for the whole buffer: a new buffer
# damaged in one corner only still shows everywhere.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

counts() { # <ppm> -> "red green blue" pure-colour pixel counts
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
r = g = b = 0
for i in range(0, len(d), 3):
    R, G, B = d[i], d[i+1], d[i+2]
    if R > 200 and G < 60 and B < 60: r += 1
    if R < 60 and G > 200 and B < 60: g += 1
    if R < 60 and G < 60 and B > 200: b += 1
print(r, g, b)
PY
}
reached() { # <ppm> <condition over $r $g $b>
    screenshot "$1" || return 1
    read -r r g b < <(counts "$1")
    eval "[[ $2 ]]"
}

start_client damage
n=1
# the window shows 60x40 of client pixels (2400), some of it under chrome
for want in '$r -gt 1500 && $g -lt 50' '$g -gt 1500 && $r -lt 50' '$b -gt 1500 && $g -lt 50'; do
    wait_client "step $n"
    await 100 reached "$XDG_RUNTIME_DIR/d$n.ppm" "$want" || { echo "step $n: red=${r:-?} green=${g:-?} blue=${b:-?}"; exit 1; }
    next
    n=$((n + 1))
done
echo "OK: unmappable surface damage repaints the whole buffer"
