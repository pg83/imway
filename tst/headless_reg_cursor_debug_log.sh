#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1 IMWAY_DEBUG_CURSOR=1
# IMWAY_DEBUG_CURSOR traces the software cursor every 120th frame: kind,
# position, whether it is over a client. The first frame is one of them,
# and its trace names the arrow over the bare desktop.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

traced() { grep -q "cursor dbg: kind 1, mp .*, overClient 0, cs 0, shape 0" "$IMWAY_LOG"; }
await 100 traced || { echo "no cursor trace in $(dump_field '^frames ' done) frames"; grep "cursor dbg" "$IMWAY_LOG" | head -3; exit 1; }

expect_alive "compositor died tracing the cursor"
echo "OK: the software cursor trace names the arrow over the desktop"
