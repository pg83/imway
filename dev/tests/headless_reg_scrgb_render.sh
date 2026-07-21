#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

dominant_surface_color() {
    python3 - "$1" <<'PY'
import collections, sys
f = open(sys.argv[1], 'rb')
assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split())
assert f.readline().strip() == b'255'
d = f.read(w*h*3)
for rgb, n in collections.Counter(zip(d[::3], d[1::3], d[2::3])).most_common():
    if 55000 <= n <= 65000:
        print(*rgb, n)
        break
PY
}

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/scrgb.ppm"
read -r r g b n < <(dominant_surface_color "$XDG_RUNTIME_DIR/scrgb.ppm")

# Extended-linear 0.75 at the default 80-nit maximum is 60 nits.
# That is PQ code 117 in the eight high bits of XR30.
[[ "$n" -gt 55000 ]]
[[ "$r" -ge 115 && "$r" -le 119 ]]
[[ "$g" -ge 115 && "$g" -le 119 ]]
[[ "$b" -ge 115 && "$b" -le 119 ]]

echo "OK: extended-linear sRGB maps normalized optical values to nits"
