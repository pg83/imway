#!/usr/bin/env bash
# Buffer release accounting: a burst of three commits releases all three
# buffers, a released buffer is reusable, and the last commit is what shows.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "burst released"
wait_client "reuse released"
sleep 0.4
screenshot "$XDG_RUNTIME_DIR/shot.ppm"

python3 - "$XDG_RUNTIME_DIR/shot.ppm" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
yellow = sum(1 for i in range(0, len(d), 3)
             if d[i] > 200 and d[i+1] > 200 and d[i+2] < 80)
print(f"yellow={yellow}")
assert yellow > 25000, "the last committed buffer is not on screen"
PY

expect_alive "compositor died on the commit burst"
echo "OK: rapid commits release every buffer and the last one shows"
