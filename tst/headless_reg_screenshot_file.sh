#!/usr/bin/env bash
# `imway screenshot PATH` on files: a raw capture file loads through the
# file path and Enter saves it as JPEG XL into IMWAY_SHOT_DIR; a file that is
# too small, one with a bad header and a truncated one each open the error
# panel, which Escape dismisses.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

# the viewer runs as a launcher command in the compositor's cwd (this dir)
cat > "$XDG_RUNTIME_DIR/shot" <<SCRIPT
#!/bin/sh
IMWAY_SHOT_DIR=$shots IMWAY_SHOT_NAME=fromfile exec "$imway_bin" screenshot "\$1"
SCRIPT
chmod +x "$XDG_RUNTIME_DIR/shot"

python3 - "$XDG_RUNTIME_DIR" <<'PY'
import struct, sys
rt = sys.argv[1]
w, h = 64, 48
pixels = bytes([0xff, 0x00, 0xff, 0xff]) * (w * h)
open(f"{rt}/good.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + pixels)
open(f"{rt}/small.shot", "wb").write(b"IMW1")
open(f"{rt}/bad.shot", "wb").write(struct.pack("<III", 0x12345678, w, h) + pixels)
open(f"{rt}/trunc.shot", "wb").write(struct.pack("<III", 0x31574d49, w, h) + pixels[: len(pixels) // 2])
PY

# typed before the launcher's field takes input, the command loses its
# first characters and sh cannot find it (status 127)
launch() { # <command typed into the launcher>
    ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
    await_typing '##launcher' || { echo "the launcher did not open for '$1'"; exit 1; }
    ctl "type $1"
    ctl "key 28 press"; ctl "key 28 release"
    await_no_imgui '##launcher' || { echo "the launcher did not run '$1'"; exit 1; }
}
viewer_up() {
    [[ -n "$(dump_field 'title=imway screenshot' id)" ]]
}
viewer_gone() {
    [[ -z "$(dump_field 'title=imway screenshot' id)" ]]
}
exits() { # <count>
    [[ "$(grep -c "exited with status" "$IMWAY_LOG")" -ge "$1" ]]
}

launch "exec ./shot ./good.shot"
await 150 viewer_up || { echo "the viewer did not open the raw file"; cat "$IMWAY_LOG"; exit 1; }
sleep 0.5
ctl "key 28 press"; ctl "key 28 release" # Enter saves
await 200 test -s "$shots/fromfile.jxl" || { echo "Enter did not save the file capture"; cat "$IMWAY_LOG"; exit 1; }
await 100 viewer_gone || { echo "the viewer stayed open after saving"; exit 1; }
await 100 exits 1 || { echo "the viewer did not exit"; exit 1; }
in_log "exited with status 0" || { echo "the file viewer failed"; cat "$IMWAY_LOG"; exit 1; }

n=1
for bad in small bad trunc; do
    launch "exec ./shot ./$bad.shot"
    await 150 viewer_up || { echo "the error panel did not open for $bad.shot"; cat "$IMWAY_LOG"; exit 1; }
    escape_until viewer_gone || { echo "Escape did not close the error panel for $bad.shot"; exit 1; }
    n=$((n + 1))
    await 100 exits $n || { echo "the error viewer for $bad.shot did not exit"; exit 1; }
done

[[ "$(grep -c "exited with status 0" "$IMWAY_LOG")" -ge 4 ]] || { echo "an error panel exited with a failure"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died during the file viewer session"
echo "OK: raw files load and save, broken files show the error panel"
