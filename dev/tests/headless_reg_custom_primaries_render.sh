#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 1000
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "raw"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/raw.ppm"
wait_client "managed"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/managed.ppm"
python3 - "$XDG_RUNTIME_DIR/raw.ppm" "$XDG_RUNTIME_DIR/managed.ppm" <<'PY'
import collections, sys
def surface(path):
    f = open(path, 'rb'); assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
    return next(rgb for rgb, n in collections.Counter(zip(d[::3], d[1::3], d[2::3])).most_common()
                if 55000 <= n <= 65000)
raw, managed = surface(sys.argv[1]), surface(sys.argv[2])
assert raw == (121, 108, 82), raw
assert managed == (123, 107, 76), managed
PY
echo "OK: custom chromaticities are transformed into the BT.2020 scene"
