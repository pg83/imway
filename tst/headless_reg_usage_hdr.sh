#!/usr/bin/env bash
# The HDR luminance options on the command line, each way they can be
# wrong: a negative minimum, peak or frame average, a frame average without
# HDR, a minimum at or above the peak. Each prints the usage and exits 2; a
# consistent set without a peak (the display's own is used) runs.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

usage() { # <args...>
    local rc=0 out
    out=$("$imway_bin" "$@" 2>&1) || rc=$?
    [[ "$rc" -eq 2 ]] || { echo "imway $* exited $rc, expected 2: $out"; exit 1; }
    grep -q "usage:" <<<"$out" || { echo "imway $* printed no usage: $out"; exit 1; }
}

usage --device headless --hdr 200 --hdr-min -1
usage --device headless --hdr 200 --hdr-peak -1
usage --device headless --hdr 200 --hdr-fall -1
usage --device headless --hdr-fall 100
usage --device headless --hdr 200 --hdr-min 100 --hdr-peak 50

rc=0
out=$(timeout 60 "$imway_bin" --device headless --socket imway-hdr-args --frames 3 --hdr 200 --hdr-min 1 --hdr-fall 100 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "a consistent HDR command line exited $rc: $out"; exit 1; }
grep -q "clean exit after" <<<"$out" || { echo "the consistent HDR run did not finish: $out"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: the HDR luminance options are checked for consistency"
