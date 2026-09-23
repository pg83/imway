#!/usr/bin/env bash
# expect-compositor-exit
# The session ends under an open screenshot editor: its connection goes
# with the compositor, its loop ends without a verdict, and the viewer
# takes that as a cancel, exiting 0 with nothing saved instead of hanging.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}

mkdir -p "$rt/shots"
IMWAY_SHOT_DIR="$rt/shots" "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 &
vpid=$!
await 150 viewer_up || { echo "the editor did not open"; cat "$rt/viewer.out"; exit 1; }

ctl "quit"
exec 3>&-

viewer_gone() {
    ! kill -0 "$vpid" 2>/dev/null
}
await 100 viewer_gone || { echo "the viewer hung after its compositor left"; cat "$rt/viewer.out"; kill "$vpid"; exit 1; }
rc=0
wait "$vpid" || rc=$?
[[ $rc -eq 0 ]] || { echo "the viewer exited $rc after its compositor left"; cat "$rt/viewer.out"; exit 1; }
[[ -z "$(ls -A "$rt/shots")" ]] || { echo "the viewer saved a file without a verdict"; ls "$rt/shots"; exit 1; }
echo "OK: a viewer whose compositor leaves exits as cancelled"
