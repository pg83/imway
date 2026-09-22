#!/usr/bin/env bash
# A sync subsurface's scale, transform, viewport, opaque region, alpha
# multiplier, colour representation and single-pixel or null buffer all
# wait in its cache for the parent commit, then apply together.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

counts() { # <ppm> -> "mix magenta": half green over red, and magenta pixels
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
mix = mag = 0
for i in range(0, len(d), 3):
    r, g, b = d[i], d[i+1], d[i+2]
    if 90 < r < 235 and 90 < g < 235 and b < 60: mix += 1
    if r > 200 and g < 60 and b > 200: mag += 1
print(mix, mag)
PY
}

reached() { # <ppm> <condition over $mix and $mag>
    screenshot "$1" || return 1
    read -r mix mag < <(counts "$1")
    eval "[[ $2 ]]"
}

# the state each step leaves once applied
applied=(''
    '$mix -gt 1500 && $mix -lt 2600 && $mag -lt 100'
    '$mix -lt 100 && $mag -gt 3000 && $mag -lt 4200'
    '$mix -lt 100 && $mag -lt 100')

start_client

for n in 1 2 3; do
    wait_client "cached $n"
    # a child-only commit changes nothing on screen: the previous step's
    # state (none before the first) is what two composed frames still show
    screenshot "$XDG_RUNTIME_DIR/c$n.ppm"
    screenshot "$XDG_RUNTIME_DIR/c$n.ppm"
    read -r mix mag < <(counts "$XDG_RUNTIME_DIR/c$n.ppm")
    if (( n == 1 )); then
        (( mix < 100 && mag < 100 )) || { echo "step 1 reached the screen before the parent commit: mix=$mix magenta=$mag"; exit 1; }
    else
        eval "[[ ${applied[n - 1]} ]]" || { echo "step $n reached the screen before the parent commit: mix=$mix magenta=$mag"; exit 1; }
    fi

    ctl "key 2 press"; ctl "key 2 release"   # KEY_1: the parent commits
    wait_client "applied $n"
    await 100 reached "$XDG_RUNTIME_DIR/a$n.ppm" "${applied[n]}" || {
        echo "step $n did not apply with the parent commit: mix=${mix:-?} magenta=${mag:-?}"
        exit 1
    }
done

echo "OK: every piece of sync subsurface state waited for the parent commit"
