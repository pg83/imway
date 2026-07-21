#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600
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
screenshot "$XDG_RUNTIME_DIR/mapped.ppm"
read -r r g b n < <(dominant_surface_color "$XDG_RUNTIME_DIR/mapped.ppm")

# PQ white is 10,000 nit. A 600-nit target is PQ code 178, allowing a
# two-code tolerance for XR30 and PPM quantization.
[[ "$n" -gt 55000 ]]
[[ "$r" -ge 176 && "$r" -le 180 ]]
[[ "$g" -ge 176 && "$g" -le 180 ]]
[[ "$b" -ge 176 && "$b" -le 180 ]]

echo "OK: 10000-nit PQ highlight maps into 600-nit output"
