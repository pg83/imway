#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=readback-fence=0
# imway-args: --device auto
# The fence of a screenshot handoff reports a lost device after the scanout
# was exported for the viewer: no viewer is spawned, and the exported
# dma-buf fd is closed rather than left open in the compositor. The session
# keeps flipping and the next Print saves.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "scanout swapchain" || { echo "no zero-copy swapchain, nothing to hand off"; cat "$IMWAY_LOG"; exit 1; }

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

dmabufs() {
    local n=0 f
    for f in /proc/"$IMWAY_PID"/fd/*; do
        [[ "$(readlink "$f" 2>/dev/null)" == /dmabuf:* ]] && n=$((n + 1))
    done
    echo "$n"
}
before=$(dmabufs)

ctl "set applications.screenshot_name lost"
ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "screenshot handoff of the scanout buffer" || { echo "no handoff"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "imway: screenshot fence failed (-4)" || { echo "the failed handoff fence was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: spawned " || { echo "a viewer was spawned for a failed handoff"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }
f0=$(flips)
advanced() { [[ "$(flips)" -ge "$((f0 + 4))" ]]; }
for i in $(seq 1 60); do
    advanced && break
    ctl "motion $((100 + i * 5)) 300"
    sleep 0.05
done
advanced || { echo "flips stopped after the failed handoff"; exit 1; }

after=$(dmabufs)
echo "dma-buf fds: before=$before after=$after"
[[ "$after" -le "$before" ]] || { echo "the exported scanout fd stayed open after the failed handoff"; ls -l /proc/"$IMWAY_PID"/fd; exit 1; }

ctl "set applications.screenshot_name kept"
ctl "key 99 press"; ctl "key 99 release"
await 200 test -s "$shots/kept.png" || { echo "the next Print did not save"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a failed handoff fence"
echo "OK: a failed handoff fence closes the exported buffer and the next Print saves"
