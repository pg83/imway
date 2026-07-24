#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Grow and shrink: 1280x800 -> a 1080p-only TV -> an 800p-only panel. The
# fullscreen client follows both configures and lands on the plane at
# every stop; the shrink direction gets the same coverage as the grow.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

start_client
wait_client "tv client sized 1280x800"

tlid=$(dump_field 'title=kms-tv' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

swap() { # <mode-set>
    ctl "kms-connector 0"
    ctl "kms-modes $1"
    ctl "kms-connector 1"
}

sized() { # <WxH> <count>
    [[ "$(grep -c "tv client sized $1" "$CLIENT_LOG")" -ge "$2" ]]
}

await 100 candidate || { echo "no direct scanout at boot"; dump_state; exit 1; }

swap 1
await 100 in_log "kms output: 1920x1080@60" || { echo "grow not taken"; cat "$IMWAY_LOG"; exit 1; }
await 100 sized 1920x1080 1 || { echo "client missed the grow"; cat "$CLIENT_LOG"; exit 1; }
await 100 candidate || { echo "no direct scanout at 1080p"; dump_state; exit 1; }

swap 2
await 100 sized 1280x800 2 || { echo "client missed the shrink"; cat "$CLIENT_LOG" "$IMWAY_LOG"; exit 1; }
await 100 candidate || { echo "no direct scanout after the shrink"; dump_state; exit 1; }

screenshot "$XDG_RUNTIME_DIR/rt.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/rt.ppm")
[[ "$dims" == "1280x800" ]] || { echo "screenshot is $dims, not 1280x800"; exit 1; }

read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/rt.ppm" 'title=kms-tv')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || { echo "client content lost after the shrink ($r $g $b)"; exit 1; }

kill -0 "$CLIENT_PID" || { echo "client died across the swaps"; cat "$CLIENT_LOG"; exit 1; }
expect_alive "compositor died on the grow-shrink round trip"
echo "OK: the display round trip lands on the plane at every stop"
