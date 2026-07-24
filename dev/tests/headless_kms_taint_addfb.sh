#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Scanout taint at the framebuffer stage: the prime import succeeds but
# AddFB2 refuses the client's buffer, the buffer gets tainted and
# composition carries the content on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

# boot is done creating framebuffers; the next AddFB2 is the client's
ctl "kms-fail-addfb 22 1"

start_client
wait_client "taint candidate mapped"

await 100 in_log "scanout AddFB2 rejected (errno 22), buffer tainted" || {
    echo "rejected AddFB2 did not taint the buffer"
    cat "$IMWAY_LOG"
    exit 1
}

sleep 1
rejections=$(grep -c "scanout AddFB2 rejected" "$IMWAY_LOG")
[[ "$rejections" -eq 1 ]] || { echo "AddFB2 retry loop: $rejections rejections"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/taint.ppm"
read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/taint.ppm" 'title=kms-taint')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || {
    echo "tainted buffer lost on screen ($r $g $b)"
    exit 1
}

expect_alive "compositor died on a rejected AddFB2"
echo "OK: AddFB2 rejection taints once, composition takes over"
