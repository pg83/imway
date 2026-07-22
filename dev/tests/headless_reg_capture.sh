#!/usr/bin/env bash
# #D-9: ext-image-copy-capture delivers the composed frame into an shm buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "capture done"
expect_client_ok "output capture did not deliver the composed frame"
echo "OK: copy-capture session produced a ready frame with the window's pixels"
