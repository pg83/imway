#!/usr/bin/env bash
# imway-args: --hdr 203
# The HDR viewer builds its linear-light scene target after its window and
# swapchain; the device refuses the first object of it (IMWAY_CHAOS=vulkan=4:
# the instance, the device, the descriptor pool and the window's surface
# come first). The viewer reports the failure and exits 1, tearing down the
# part of the target it had not built without faulting on it; the next
# viewer opens as usual.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

rc=0
env IMWAY_SHOT_COLOR=1:203 IMWAY_CHAOS=vulkan=4 timeout 60 "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 || rc=$?
[[ $rc -eq 1 ]] || { echo "the refused scene target did not fail the viewer with 1 (rc=$rc)"; cat "$rt/viewer.out"; exit 1; }
grep -q "vulkan error -2" "$rt/viewer.out" || { echo "the refusal was not reported"; cat "$rt/viewer.out"; exit 1; }

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
await 100 viewer_gone || { echo "the failed viewer left its window"; exit 1; }

env IMWAY_SHOT_COLOR=1:203 "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 &
vpid=$!
await 150 viewer_up || { echo "the next HDR viewer did not open"; cat "$rt/viewer.out"; exit 1; }
ctl "key 1 press"; ctl "key 1 release" # Escape
await 100 viewer_gone || { echo "Escape did not close the next viewer"; exit 1; }
rc=0
wait "$vpid" || rc=$?
[[ $rc -eq 0 ]] || { echo "the next HDR viewer exited $rc"; cat "$rt/viewer.out"; exit 1; }

expect_alive "compositor died while the HDR viewer failed"
echo "OK: a refused HDR scene target fails the viewer cleanly"
