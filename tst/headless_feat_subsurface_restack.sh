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

# The client prints on its commit, but the frame carrying that content is a
# round trip away and an instrumented build widens the gap: take fresh
# readbacks until the counts hold, instead of shooting once and hoping.
settled() { # <ppm> <cond over g and b> -> "green blue"
    local g b i

    for ((i = 0; i < 40; i++)); do
        if screenshot "$1"; then
            read -r g b < <(count "$1")

            if eval "[[ $2 ]]"; then
                echo "$g $b"

                return 0
            fi
        fi

        sleep 0.2
    done

    echo "green=${g:-?} blue=${b:-?}" >&2

    return 1
}

start_client
wait_client "state1"
read -r g1 b1 < <(settled "$XDG_RUNTIME_DIR/s1.ppm" '"$g" -gt 1000 && "$b" -gt 1000') || {
    echo "the subsurface pile never reached the readback"; exit 1; }

wait_client "state2"
read -r g2 b2 < <(settled "$XDG_RUNTIME_DIR/s2.ppm" "\"\$g\" -gt $g1 && \"\$b\" -lt $b1") || {
    echo "place_below did not restack the subsurfaces"; exit 1; }

echo "state1 green=$g1 blue=$b1 → state2 green=$g2 blue=$b2"
echo "OK: subsurface place_below restacked (green uncovered, blue occluded)"
