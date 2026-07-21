#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/gamma22.ppm"
python3 - "$XDG_RUNTIME_DIR/gamma22.ppm" <<'PY'
import collections, sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(w*h*3)
colors = collections.Counter(zip(d[::3], d[1::3], d[2::3]))
rgb, n = next((rgb, n) for rgb, n in colors.most_common() if 55000 <= n <= 65000)
assert 106 <= rgb[0] <= 110 and 106 <= rgb[1] <= 110 and 106 <= rgb[2] <= 110, (rgb, n)
PY

echo "OK: gamma 2.2 EOTF maps electrical values to absolute nits"
