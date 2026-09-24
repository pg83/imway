#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-eintr=2 IMWAY_FAKE_KMS_NO_PRIME=1
# The screenshot file's writes are interrupted by a signal, twice: each is
# retried, and the file the viewer reads is whole.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
ctl "set applications.screenshot_name whole"
await 100 in_log "control: set applications.screenshot_name" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print
await 200 test -s "$shots/whole.png" || { echo "the interrupted file was not saved"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: screenshot readback failed" || { echo "an interrupted write dropped the capture"; cat "$IMWAY_LOG"; exit 1; }

# the PNG is the whole output: its header says so
read -r w h < <(python3 - "$shots/whole.png" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(24)
assert d[:8] == b'\x89PNG\r\n\x1a\n'
print(*struct.unpack('>II', d[16:24]))
PY
)
[[ "$w" == 1280 && "$h" == 800 ]] || { echo "the saved screenshot is ${w}x${h}"; exit 1; }

expect_alive "compositor died on interrupted screenshot writes"
echo "OK: interrupted screenshot writes are retried and the file is whole"
