#!/usr/bin/env bash
# imway-args: --hdr 300
# zwlr-screencopy of an HDR KMS output: the 10-bit PQ scanout is narrowed to the
# 8-bit XRGB the client's buffer holds, the same PQ code values the
# compositor's own capture of that output reads.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "10-bit scanout" || { echo "no 10-bit scanout"; cat "$IMWAY_LOG"; exit 1; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_screencopy"
start_client "$XDG_RUNTIME_DIR/copy.ppm"
wait_client "screencopy dumped"
screenshot "$XDG_RUNTIME_DIR/own.ppm"

python3 - "$XDG_RUNTIME_DIR/copy.ppm" "$XDG_RUNTIME_DIR/own.ppm" <<'PY'
import sys

def load_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, h, f.read(w * h * 3)

cw, ch, copy = load_ppm(sys.argv[1])
w, h, own = load_ppm(sys.argv[2])
assert (cw, ch) == (w, h), f"copy {cw}x{ch} is not the {w}x{h} output"
samples = range(0, w * h * 3, 3 * 13)
same = sum(1 for i in samples if all(abs(copy[i + c] - own[i + c]) <= 1 for c in range(3)))
# the window's magenta as PQ BT.2020 is not the sRGB code it was drawn with
magenta = sum(1 for i in samples if copy[i:i + 3] == b'\xff\x00\xff')
print(f"samples={len(samples)} same={same} srgb-magenta={magenta}")
assert same >= len(samples) * .95, "the screencopy is not the output's frame"
assert magenta == 0, "the screencopy carries sRGB codes, not the output's PQ"
PY

kill "$CLIENT_PID" 2>/dev/null || true
expect_alive "compositor died copying an HDR frame"
echo "OK: screencopy of an HDR output delivers the frame's PQ code values"
