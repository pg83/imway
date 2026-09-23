#!/usr/bin/env bash
# A sync subsurface caches a wl_shm buffer whose stride is too short for its
# rows: the compositor cannot read it, and the parent's commit leaves the
# child empty instead of showing stale or garbage content.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

green() { # <ppm> -> pure green pixel count
    python3 - "$1" <<'PY'
import sys
f = open(sys.argv[1], 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
print(sum(1 for i in range(0, len(d), 3) if d[i] < 60 and d[i+1] > 200 and d[i+2] < 60))
PY
}
reached() { # <ppm> <condition over $g>
    screenshot "$1" || return 1
    g=$(green "$1")
    eval "[[ $2 ]]"
}

start_client unusable-cached
wait_client "step 1"
await 100 reached "$XDG_RUNTIME_DIR/u1.ppm" '$g -gt 3000' || { echo "the green child never showed (${g:-?})"; exit 1; }
next
wait_client "step 2"
await 100 reached "$XDG_RUNTIME_DIR/u2.ppm" '$g -lt 50' || { echo "the unreadable buffer left the child showing (${g:-?})"; exit 1; }
expect_alive
echo "OK: an unreadable cached buffer empties the child"
