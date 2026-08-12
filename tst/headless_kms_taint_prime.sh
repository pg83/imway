#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto
# Scanout taint at the earliest import stage: the KMS side refuses the
# prime fd import of the client's framebuffer, the buffer gets tainted at
# once and composition carries the content on.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

# skip 1: the buffer-create importability probe passes, the direct-scanout
# import is the one that fails
ctl "kms-fail-prime 22 1 1"

start_client
wait_client "taint candidate mapped"

await 100 in_log "scanout prime import rejected (errno 22), buffer tainted" || {
    echo "rejected prime import did not taint the buffer"
    cat "$IMWAY_LOG"
    exit 1
}

sleep 1
rejections=$(grep -c "scanout prime import rejected" "$IMWAY_LOG")
[[ "$rejections" -eq 1 ]] || { echo "import retry loop: $rejections rejections"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/taint.ppm"
read -r r g b _ < <(surface_mean "$XDG_RUNTIME_DIR/taint.ppm" 'title=kms-taint')
[[ "$r" -gt 150 && "$g" -lt 90 ]] || {
    echo "tainted buffer lost on screen ($r $g $b)"
    exit 1
}

expect_alive "compositor died on a rejected prime import"
echo "OK: prime import rejection taints once, composition takes over"
