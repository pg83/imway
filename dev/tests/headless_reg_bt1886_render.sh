#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/bt1886.ppm"
python3 - "$XDG_RUNTIME_DIR/bt1886.ppm" <<'PY'
import collections, sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(w*h*3)
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
rgb, n = next((rgb, n) for rgb, n in colors.most_common() if 55000 <= n <= 65000)
assert 111 <= rgb[0] <= 115 and 111 <= rgb[1] <= 115 and 111 <= rgb[2] <= 115, (rgb, n)
PY

echo "OK: BT.1886 EOTF uses its black and white luminance"
