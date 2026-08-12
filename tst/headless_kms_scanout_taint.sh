#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Scanout taint end to end: the plane rejects the client's framebuffer with
# a permanent errno on its first direct flip, the buffer gets tainted, the
# flip is never retried and composition carries the content on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

# arm before the candidate exists: only its framebuffer will be new
ctl "kms-fail-new-fb 22"

start_client
wait_client "taint candidate mapped"

tlid=$(dump_field 'title=kms-taint' id)

candidate() {
    [[ "$(dump_field '^scanout' candidate)" == "$tlid" ]]
}

await 100 candidate || { echo "fullscreen dmabuf never became a candidate"; dump_state; exit 1; }

await 100 in_log "scanout flip rejected (errno 22), buffer tainted" || {
    echo "rejected flip did not taint the buffer"
    cat "$IMWAY_LOG"
    exit 1
}

# the whole point: one rejection, zero retries
sleep 1
rejections=$(grep -c "scanout flip rejected" "$IMWAY_LOG")
[[ "$rejections" -eq 1 ]] || { echo "flip retry loop: $rejections rejections"; exit 1; }

# composition carries the tainted buffer: the red survives on screen
screenshot "$XDG_RUNTIME_DIR/taint.ppm"
read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/taint.ppm" 'title=kms-taint')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || {
    echo "tainted buffer lost on screen ($r $g $b)"
    exit 1
}

expect_alive "compositor died on a rejected scanout flip"
echo "OK: permanent flip rejection taints once, composition takes over"
