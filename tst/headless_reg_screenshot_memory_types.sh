#!/usr/bin/env bash
# The screenshot viewer asks the device for a memory type that fits its
# texture and the device has none (IMWAY_CHAOS=memory-types=1 empties the
# viewer's first query): the viewer reports it and exits 1 instead of
# allocating from a type the texture cannot live in.
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
env IMWAY_CHAOS=memory-types=1 timeout 20 "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 || rc=$?
[[ $rc -eq 1 ]] || { echo "a device without a fitting memory type did not fail the viewer (rc=$rc)"; cat "$rt/viewer.out"; exit 1; }
grep -q "no vulkan memory type fits" "$rt/viewer.out" || { echo "the missing memory type was not reported"; cat "$rt/viewer.out"; exit 1; }

viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
await 100 viewer_gone || { echo "the failed viewer left its window"; exit 1; }

expect_alive "compositor died while the viewer failed"
echo "OK: a device without a fitting memory type fails the viewer"
