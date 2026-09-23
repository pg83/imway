#!/usr/bin/env bash
# The screenshot viewer saving straight away (no editor) runs out of memory
# for its encoder (the viewer's IMWAY_CHAOS=encoder-alloc=K): libpng's write
# struct or its info struct, libjxl's encoder or its frame settings. The
# save fails without a file, the window a save never shows comes up with
# the error panel, and Escape ends the viewer cleanly. A save with memory
# to spare writes its file without a window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"
shots="$rt/shots"
mkdir -p "$shots"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
save() { # <format> <encoder-alloc K, or none>
    local chaos=""
    [[ "$2" != none ]] && chaos="encoder-alloc=$2"
    env IMWAY_CHAOS="$chaos" IMWAY_SHOT_ACTION=save IMWAY_SHOT_FORMAT="$1" IMWAY_SHOT_DIR="$shots" IMWAY_SHOT_NAME="$1-$2" \
        "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 &
    vpid=$!
}

for case in png:0 png:1 jxl:0 jxl:1; do
    format=${case%%:*}
    k=${case##*:}
    save "$format" "$k"
    await 150 viewer_up || { echo "$case: the failed save did not bring up the error panel"; cat "$rt/viewer.out"; exit 1; }
    escape_until viewer_gone || { echo "$case: Escape did not close the error panel"; exit 1; }
    rc=0
    wait "$vpid" || rc=$?
    [[ $rc -eq 0 ]] || { echo "$case: the viewer exited $rc"; cat "$rt/viewer.out"; exit 1; }
    [[ -z "$(ls -A "$shots")" ]] || { echo "$case: a failed encoder saved a file"; ls "$shots"; exit 1; }
done

save png none
rc=0
wait "$vpid" || rc=$?
[[ $rc -eq 0 && -s "$shots/png-none.png" ]] || { echo "a save with memory to spare failed (rc=$rc)"; cat "$rt/viewer.out"; exit 1; }

expect_alive "compositor died while the viewer's encoder failed"
echo "OK: an encoder without memory shows the error panel and saves nothing"
