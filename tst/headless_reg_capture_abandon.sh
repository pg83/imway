#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=readback-busy=300
# Output copies abandoned while their readback is on the GPU (the fence
# reads busy for a few hundred polls): an ext-image-copy-capture frame
# destroyed in flight is dropped from the readback and the session's next
# frame captures the window; an ext frame and a zwlr-screencopy frame whose
# buffer is destroyed in flight fail once the readback lands.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "abandoned in flight"
wait_client "next capture ready"
wait_client "bufferless capture failed"
wait_client "bufferless screencopy failed"
expect_client_ok "the abandoned copies did not end as expected"
expect_alive "compositor died finishing abandoned copies' readbacks"
echo "OK: abandoned in-flight copies are dropped or failed, the next one lands"
