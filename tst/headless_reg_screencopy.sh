#!/usr/bin/env bash
# #D-10: zwlr-screencopy v3 compat delivers the composed frame.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "screencopy done"
expect_client_ok "zwlr-screencopy did not deliver the composed frame"
echo "OK: screencopy copied the frame with the window's pixels"
