#!/usr/bin/env bash
# place_above a sibling and place_below the parent restack on the parent's
# commit; the subcompositor may be destroyed while its subsurfaces live on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

counts() { # <ppm> -> "green blue"
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
g = b = 0
for i in range(0, len(d), 3):
    r, gg, bb = d[i], d[i+1], d[i+2]
    if r < 60 and gg > 200 and bb < 60: g += 1
    if r < 60 and gg < 60 and bb > 200: b += 1
print(g, b)
PY
}
reached() { # <ppm> <condition over $g and $b>
    screenshot "$1" || return 1
    read -r g b < <(counts "$1")
    eval "[[ $2 ]]"
}
start_client restack
n=1
# 100x100 squares overlapping by 60x60: the one on top shows whole
for want in '$b -gt 9500 && $g -gt 6000 && $g -lt 6800' \
            '$g -gt 9500 && $b -gt 6000 && $b -lt 6800' \
            '$g -gt 9500 && $b -lt 100'; do
    wait_client "step $n"
    await 100 reached "$XDG_RUNTIME_DIR/s$n.ppm" "$want" || { echo "step $n: green=${g:-?} blue=${b:-?}"; exit 1; }
    next
    n=$((n + 1))
done
echo "OK: subsurfaces restack above a sibling and below the parent"
