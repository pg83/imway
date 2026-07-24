#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_REJECT_COLOR=1
# imway-args: --device auto --hdr 300
# The connector refuses the HDR color configuration: the first modeset
# falls back to SDR on the same framebuffer and the session lights up.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 in_log "falling back to SDR" || { echo "no HDR fallback"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output" || { echo "session did not light up"; cat "$IMWAY_LOG"; exit 1; }

frames() { dump_state | awk '/^frames/ { print $2 }' | cut -d= -f2; }

f0=$(frames)
ctl "key 2 press"; ctl "key 2 release"
sleep 1

[[ "$(frames)" -gt "$f0" ]] || { echo "no frames after the fallbacks"; exit 1; }

expect_alive "compositor died on a color-rejecting connector"
echo "OK: HDR -> SDR -> 8-bit, the session survives the ladder"
