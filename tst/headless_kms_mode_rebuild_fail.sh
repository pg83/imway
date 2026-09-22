#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=scanout=10
# imway-args: --device auto
# A display swapped for one with other modes, and the scanout rebuild at
# its size fails on the GPU: the output stays at the old size on a rebuilt
# swapchain and says the new display refuses it. Swapped back, the old mode
# is offered again and the session lights up and flips.
# The boot swapchain is two 10-bit buffers of five scanout Vulkan calls
# each; scanout=10 fails the first call of the rebuild.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "imway: 10-bit scanout" || { echo "the boot swapchain is not the 10-bit one the fault count assumes"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }

ctl "kms-modes 1"
ctl "kms-connector 1"
await 100 in_log "scanout rebuild failed at 1920x1080, staying at 1280x800" || { echo "the failed rebuild was not reported"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "reconnected display refuses the current mode" || { echo "the refusal was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "kms output: 1920x1080" || { echo "the output moved to a mode it has no swapchain for"; exit 1; }

# the original display back: its mode is offered again
ctl "kms-connector 0"
await 50 test "$(grep -c "connector disconnected" "$IMWAY_LOG")" -ge 2 || { echo "second disconnect unnoticed"; exit 1; }
ctl "kms-modes 0"
ctl "kms-connector 1"
await 100 in_log "connector reconnected, remodeset" || { echo "the old display did not come back"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips on the rebuilt swapchain"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/back.ppm"
dims=$(awk 'NR == 2 { print $1 "x" $2; exit }' "$XDG_RUNTIME_DIR/back.ppm")
[[ "$dims" == "1280x800" ]] || { echo "screenshot is $dims, not 1280x800"; exit 1; }

expect_alive "compositor died on a failed scanout rebuild"
echo "OK: a failed rebuild keeps the old size, the old display lights up again"
