#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=readback-busy=1000
# An ext-image-copy-capture frame destroyed while its readback is on the
# GPU (the fence reads busy for a thousand polls) is dropped from the
# readback, and the session's next frame captures the window.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client abandon
wait_client "abandoned in flight"
wait_client "next capture ready"
expect_client_ok "the abandoned capture did not end as expected"
expect_alive "compositor died finishing an abandoned capture's readback"
echo "OK: an abandoned in-flight capture is dropped and the next one lands"
