#!/usr/bin/env bash
# The command line of a second compositor started from here: every rejected
# argument prints the usage and exits 2, a missing runtime directory exits 1,
# --list runs the KMS probe, and --frames with --screenshot renders a fixed
# number of frames, writes the last one and exits cleanly.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

usage() { # <args...>
    local rc=0 out
    out=$("$imway_bin" "$@" 2>&1) || rc=$?
    [[ "$rc" -eq 2 ]] || { echo "imway $* exited $rc, expected 2: $out"; exit 1; }
    grep -q "usage:" <<<"$out" || { echo "imway $* printed no usage: $out"; exit 1; }
}

usage --device headless --mode
usage --device headless --scale 0
usage --device headless --scale -1
usage --device headless --rgb-range sideways
usage --device headless --no-such-flag
usage --device headless --hdr -1
usage --device headless --bpc 9
usage --device headless --hdr-min 100 --hdr-peak 50
usage --device headless --hdr-peak 100
usage --device headless --hdr 200 --hdr-peak 100 --hdr-fall 200
usage --device headless --
usage screenshot

rc=0
out=$(env -u XDG_RUNTIME_DIR "$imway_bin" --device headless 2>&1) || rc=$?
[[ "$rc" -eq 1 ]] || { echo "no runtime dir exited $rc, expected 1: $out"; exit 1; }
grep -q "XDG_RUNTIME_DIR" <<<"$out" || { echo "no runtime dir: unexpected output: $out"; exit 1; }

rc=0
out=$("$imway_bin" --list 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "--list exited $rc: $out"; exit 1; }

# a fixed-length headless run next to this one, on its own socket
rc=0
out=$("$imway_bin" --device headless --socket imway-frames --frames 3 --screenshot "$XDG_RUNTIME_DIR/last.ppm" --xkb-layout us --xkb-options "" --dpms 1 2>&1) || rc=$?
[[ "$rc" -eq 0 ]] || { echo "the fixed-length run exited $rc: $out"; exit 1; }
grep -q "clean exit after 3 frames" <<<"$out" || { echo "the fixed-length run did not stop at 3 frames: $out"; exit 1; }
[[ -s "$XDG_RUNTIME_DIR/last.ppm" ]] || { echo "no screenshot from the fixed-length run"; exit 1; }
[[ "$(head -c 2 "$XDG_RUNTIME_DIR/last.ppm")" == "P6" ]] || { echo "the screenshot is not a PPM"; exit 1; }

expect_alive "the test compositor died while others ran"
echo "OK: usage errors, the runtime dir check, --list and the fixed-length run"
