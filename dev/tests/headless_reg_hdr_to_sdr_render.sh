#!/usr/bin/env bash
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
screenshot "$XDG_RUNTIME_DIR/sdr.ppm"
read -r r g b n < <(dominant_surface_color "$XDG_RUNTIME_DIR/sdr.ppm")

# A 10,000-nit BT.2020 green is far outside SDR. It must be desaturated into
# the BT.709 volume, not independently clipped to electric green.
[[ "$n" -gt 55000 ]]
[[ "$r" -gt 200 && "$g" -gt 200 && "$b" -gt 200 ]]
[[ $((r - g)) -ge -8 && $((r - g)) -le 8 ]]
[[ $((b - g)) -ge -8 && $((b - g)) -le 8 ]]

echo "OK: HDR wide-gamut highlight maps into SDR volume"
