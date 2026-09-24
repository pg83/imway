#!/usr/bin/env bash
# The device is lost while the renderer waits on a fence it cannot go on
# without: the readback of the final frame that --screenshot saves (on a
# display without a cursor plane, whose image would be waited for first). The
# policy for a lost device is to log it and leave with status 1 at once,
# not to hang on the wait or write a screenshot of stale bytes.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
shot="$XDG_RUNTIME_DIR/lost.ppm"

rc=0
out=$(env IMWAY_CHAOS=gpu-wait=0 IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1 timeout 60 "$imway_bin" --device auto --socket imway-wait --frames 3 --screenshot "$shot" 2>&1) || rc=$?
[[ "$rc" -ne 124 ]] || { echo "a lost device hung the readback: $out"; exit 1; }
[[ "$rc" -eq 1 ]] || { echo "a lost device during the readback exited $rc: $out"; exit 1; }
grep -q "imway: gpu fatal in readback (-4), exiting" <<<"$out" || { echo "the lost device was not reported: $out"; exit 1; }
[[ ! -s "$shot" ]] || { echo "a screenshot was written after the device was lost"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: a device lost during a blocking readback ends the process with status 1"
