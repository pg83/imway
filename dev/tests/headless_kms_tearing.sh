#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# wp-tearing-control end to end: a fullscreen client that opted into async
# presentation gets its direct-scanout flips submitted as async page flips.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "tearing candidate mapped"

tlid=$(dump_field 'title=kms-tearing' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "fullscreen dmabuf never became a candidate"; dump_state; exit 1; }

await 100 in_log "fake-kms: async page flip" || {
    echo "tearing hint never produced an async flip"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on async flips"
echo "OK: the tearing hint reaches the plane as async page flips"
