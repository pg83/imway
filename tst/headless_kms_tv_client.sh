#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# The TV case end to end: a fullscreen client is on the plane at 1280x800,
# the display is swapped for a 1080p-only one, the client follows the
# configure with a new buffer and lands back on the plane at 1920x1080
# with its red filling the whole screen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "tv client sized 1280x800"

tlid=$(dump_field 'title=kms-tv' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "no direct scanout before the swap"; dump_state; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-modes 1"
ctl "kms-connector 1"

await 100 in_log "kms output: 1920x1080@60" || {
    echo "new display's mode not taken on reconnect"
    cat "$IMWAY_LOG"
    exit 1
}

# the client answers the configure with a 1080p buffer...
wait_client "tv client sized 1920x1080"

# ...and returns to the plane at the new size
await 100 candidate || { echo "no direct scanout after the swap"; dump_state; exit 1; }

# the red covers the whole new screen
screenshot "$XDG_RUNTIME_DIR/tv.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/tv.ppm")
[[ "$dims" == "1920x1080" ]] || { echo "screenshot is $dims, not 1920x1080"; exit 1; }

read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/tv.ppm" 'title=kms-tv')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || {
    echo "client content lost after the swap ($r $g $b)"
    exit 1
}

kill -0 "$CLIENT_PID" || { echo "client died across the display swap"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died resizing a fullscreen client"
echo "OK: the display swap round-trips through the client back to the plane"
