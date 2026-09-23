#!/usr/bin/env bash
# `imway screenshot` given what it cannot use. A file that does not exist and
# shared-buffer metadata it cannot parse (too few, empty, negative or
# run-together fields, a field past 64 bits, a GPU id that is not 32
# lowercase hex digits, a zero width, height, stride or size), or whose
# buffer cannot be opened, each open the 480x180 error panel, which Escape
# dismisses with status 0. Well-formed metadata naming a GPU no device has
# gets no window: the viewer says so and exits 1. Colour metadata it only
# partly understands falls back to SDR and still shows the image.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
rt="$XDG_RUNTIME_DIR"

python3 - "$rt" <<'PY'
import struct, sys
w, h = 64, 48
open(f"{sys.argv[1]}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + bytes([0xff, 0, 0xff, 0xff]) * (w * h))
PY

uuid=00112233445566778899aabbccddeeff
viewer_size() {
    echo "$(dump_field 'title=imway screenshot' client_w) $(dump_field 'title=imway screenshot' client_h)"
}
viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}

open_viewer() { # <what> <path> [env...]: runs the viewer, leaves its pid in vpid
    local what=$1 path=$2
    shift 2
    env "$@" "$imway_bin" screenshot "$path" >"$rt/viewer.out" 2>&1 &
    vpid=$!
    await 150 viewer_up || { echo "$what: no window"; cat "$rt/viewer.out"; exit 1; }
    sleep 0.3
}
close_viewer() { # <what>
    ctl "key 1 press"; ctl "key 1 release" # Escape
    await 100 viewer_gone || { echo "$1: Escape did not close the viewer"; exit 1; }
    local rc=0
    wait "$vpid" || rc=$?
    [[ $rc -eq 0 ]] || { echo "$1: the viewer exited $rc"; cat "$rt/viewer.out"; exit 1; }
}
error_panel() { # <what> <path> [env...]
    open_viewer "$@"
    [[ "$(viewer_size)" == "480 180" ]] || { echo "$1: not the error panel ($(viewer_size))"; exit 1; }
    close_viewer "$1"
}
image() { # <what> <path> [env...]
    open_viewer "$@"
    [[ "$(viewer_size)" != "480 180" ]] || { echo "$1: the image did not load"; exit 1; }
    close_viewer "$1"
}

error_panel "a missing file" "$rt/missing.shot"
error_panel "too few fields" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44
error_panel "a field past 64 bits" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:99999999999999999999999:$uuid
error_panel "a GPU id that is not hex" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:zz112233445566778899aabbccddeeff
error_panel "an empty field" "$rt/good.shot" IMWAY_SHOT_DMABUF=64::44:0:256:0:12288:$uuid
error_panel "a negative field" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:-48:44:0:256:0:12288:$uuid
error_panel "two fields run together" "$rt/good.shot" IMWAY_SHOT_DMABUF=64x48:44:0:256:0:12288:$uuid
error_panel "a GPU id too short" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:00112233445566778899aabbccddee
error_panel "a GPU id in capitals" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:00112233445566778899AABBCCDDEEFF
error_panel "a GPU id with punctuation" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:00112233-45566778899aabbccddeeff
error_panel "a zero stride" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:0:0:12288:$uuid
error_panel "a zero width" "$rt/good.shot" IMWAY_SHOT_DMABUF=0:48:44:0:256:0:12288:$uuid
error_panel "a zero height" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:0:44:0:256:0:12288:$uuid
error_panel "a zero size" "$rt/good.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:0:$uuid
error_panel "a buffer that cannot be opened" "$rt/missing.shot" IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:$uuid

image "colour without a volume" "$rt/good.shot" IMWAY_SHOT_COLOR=0:203
image "colour without a transfer" "$rt/good.shot" IMWAY_SHOT_COLOR=garbage

rc=0
env IMWAY_SHOT_DMABUF=64:48:44:0:256:0:12288:$uuid "$imway_bin" screenshot "$rt/good.shot" >"$rt/viewer.out" 2>&1 || rc=$?
[[ $rc -eq 1 ]] || { echo "an unknown GPU did not fail the viewer (rc=$rc)"; cat "$rt/viewer.out"; exit 1; }
grep -q "shared screenshot gpu is unavailable" "$rt/viewer.out" || { echo "the unknown GPU was not reported"; cat "$rt/viewer.out"; exit 1; }

expect_alive "compositor died while the viewer refused its inputs"
echo "OK: unusable viewer inputs open the error panel or are reported"
