#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The positive direct-scanout path: a lone fullscreen dmabuf toplevel goes
# straight to the plane, flips keep coming and no rejection is logged.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "fullscreen dmabuf never became a candidate"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

await 100 advanced || { echo "no flips on the direct path"; exit 1; }

! in_log "rejected" || { echo "direct scanout logged a rejection"; cat "$IMWAY_LOG"; exit 1; }

# composition is forced for the screenshot itself; the content must match
screenshot "$XDG_RUNTIME_DIR/direct.ppm"
read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/direct.ppm" 'title=kms-taint')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || {
    echo "client content lost ($r $g $b)"
    exit 1
}

expect_alive "compositor died on the direct scanout path"
echo "OK: direct scanout engages and flips"
