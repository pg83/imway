#!/usr/bin/env bash
# Null-attach unmap and remap: the window must disappear (state and pixels),
# and a fresh initial commit must bring it back with a new configure.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

red_count() { # <ppm>
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
print(sum(1 for i in range(0, len(d), 3) if d[i] > 200 and d[i+1] < 60 and d[i+2] < 60))
PY
}

start_client
wait_client "both mapped"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/s1.ppm"
r1=$(red_count "$XDG_RUNTIME_DIR/s1.ppm")
(( r1 > 40000 )) || { echo "main window not visible (red=$r1)"; exit 1; }
[[ $(dump_field 'app_id=unmap' mapped) == 1 ]] || { echo "not mapped in dump"; exit 1; }

ctl "key 22 press"; ctl "key 22 release"   # KEY_U
wait_client "unmapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/s2.ppm"
r2=$(red_count "$XDG_RUNTIME_DIR/s2.ppm")
m2=$(dump_field 'app_id=unmap' mapped)
echo "after unmap: red=$r2 mapped=$m2"
(( r2 < 100 )) || { echo "unmapped window still on screen (red=$r2)"; exit 1; }
[[ "$m2" == 0 ]] || { echo "dump still says mapped=1"; exit 1; }

ctl "key 19 press"; ctl "key 19 release"   # KEY_R
wait_client "remapped"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/s3.ppm"
r3=$(red_count "$XDG_RUNTIME_DIR/s3.ppm")
m3=$(dump_field 'app_id=unmap' mapped)
echo "after remap: red=$r3 mapped=$m3"
(( r3 > 40000 )) || { echo "window did not come back (red=$r3)"; exit 1; }
[[ "$m3" == 1 ]] || { echo "dump says mapped=$m3 after remap"; exit 1; }

expect_alive "compositor died across unmap/remap"
echo "OK: null-attach unmapped and a fresh initial commit remapped the window"
