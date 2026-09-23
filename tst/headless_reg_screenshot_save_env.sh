#!/usr/bin/env bash
# `imway screenshot` saving with what its environment leaves out or
# overstates: a lossy JPEG XL quality below 1 or above 100 is held to that
# range (the low one saves far smaller than the high one), and with neither
# a screenshot directory nor XDG_PICTURES_DIR the file lands under
# $HOME/Pictures/screenshots.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import random, struct, sys
w, h = 256, 192
rnd = random.Random(7)
# noise, so the quality setting has something to throw away
pixels = bytes(b for _ in range(w * h) for b in (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), 255))
open(f"{sys.argv[1]}/noise.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + pixels)
PY

save() { # <what> <file that must appear> [env...]
    local what=$1 file=$2 rc=0
    shift 2
    env -u XDG_PICTURES_DIR IMWAY_SHOT_ACTION=save "$@" "$imway_bin" screenshot "$rt/noise.shot" >"$rt/viewer.out" 2>&1 || rc=$?
    [[ $rc -eq 0 && -s "$file" ]] || { echo "$what: nothing saved at $file (rc=$rc)"; cat "$rt/viewer.out"; exit 1; }
}

save "quality 0" "$rt/shots/low.jxl" IMWAY_SHOT_DIR="$rt/shots" IMWAY_SHOT_NAME=low IMWAY_SHOT_FORMAT=jxl IMWAY_SHOT_LOSSLESS=0 IMWAY_SHOT_QUALITY=0
save "quality 150" "$rt/shots/high.jxl" IMWAY_SHOT_DIR="$rt/shots" IMWAY_SHOT_NAME=high IMWAY_SHOT_FORMAT=jxl IMWAY_SHOT_LOSSLESS=0 IMWAY_SHOT_QUALITY=150
low=$(stat -c %s "$rt/shots/low.jxl"); high=$(stat -c %s "$rt/shots/high.jxl")
echo "jxl sizes: quality 0 -> $low, quality 150 -> $high"
(( low * 2 < high )) || { echo "the clamped qualities did not encode differently"; exit 1; }

mkdir -p "$rt/home"
save "no directory at all" "$rt/home/Pictures/screenshots/homed.png" HOME="$rt/home" IMWAY_SHOT_DIR= IMWAY_SHOT_NAME=homed IMWAY_SHOT_FORMAT=png

expect_alive "compositor died while the viewer saved"
echo "OK: out-of-range quality is clamped and HOME is the last directory fallback"
