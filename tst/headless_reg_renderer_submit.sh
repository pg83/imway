#!/usr/bin/env bash
# The GPU queue refusing the renderer's work. A refused frame is fatal: the
# session ends at once with the reason on record, never hanging on a frame
# fence nothing will signal. A refused screenshot readback costs that
# screenshot only, and a refused cursor shape leaves a transparent cursor
# rather than a session frozen on its fence. A screenshot path that cannot
# be written costs only that file.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

rc=0
out=$(env IMWAY_CHAOS=frame-submit=1 timeout 60 "$imway_bin" --device auto --socket imway-submit --frames 3 2>&1) || rc=$?
[[ "$rc" -ne 124 ]] || { echo "a refused frame hung the compositor: $out"; exit 1; }
grep -q "Vulkan queue submit failed" <<<"$out" || { echo "the refused frame was not reported (rc=$rc): $out"; exit 1; }
! grep -q "clean exit after 3 frames" <<<"$out" || { echo "the session ran on after a refused frame: $out"; exit 1; }

shot="$XDG_RUNTIME_DIR/refused.ppm"
rc=0
out=$(env IMWAY_CHAOS=readback-submit=1 timeout 60 "$imway_bin" --device auto --socket imway-submit --frames 3 --screenshot "$shot" 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "a refused readback exited $rc: $out"; exit 1; }
grep -q "readback submit failed" <<<"$out" || { echo "the refused readback was not reported: $out"; exit 1; }
[[ ! -s "$shot" ]] || { echo "a screenshot was written from a refused readback"; exit 1; }

# a screenshot path that cannot be written costs nothing but the file
ctl "screenshot /nonexistent-imway-dir/shot.ppm"
await 50 in_log "screenshot by command: /nonexistent-imway-dir/shot.ppm" || { echo "the unwritable screenshot was not taken up"; exit 1; }
[[ ! -e /nonexistent-imway-dir ]] || { echo "the screenshot created a directory"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm" || { echo "the next screenshot failed"; exit 1; }

kms_boot IMWAY_CHAOS=cursor-submit=1 --
boot_rc 0 "refused cursor shape"
boot_has "cursor rasterize submit failed" "refused cursor shape"
boot_has "clean exit after" "refused cursor shape"

expect_alive "the scenario's own compositor died"
echo "OK: refused GPU submits end the session or cost only their own work"
