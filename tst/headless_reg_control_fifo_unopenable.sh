#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=control-open=1
# expect-startup-exit
# The control FIFO is made but its read end cannot be opened (a process
# out of descriptors): the compositor refuses to start, naming the failed
# open, and the FIFO it made leaves the filesystem on the way out.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" -eq 1 ]] || { echo "an unopenable control FIFO exited $IMWAY_RC"; cat "$IMWAY_LOG"; exit 1; }
grep "imway: fatal" "$IMWAY_LOG" | grep -qF '*fd >= 0' || { echo "the refusal does not name the open"; cat "$IMWAY_LOG"; exit 1; }
! in_log "control FIFO:" || { echo "a control FIFO was reported up"; cat "$IMWAY_LOG"; exit 1; }
# lib.sh's writer made a plain file at the path by now, if nothing was there
[[ ! -p "$IMWAY_CTL" ]] || { echo "the control FIFO outlived the refusal"; exit 1; }
echo "OK: a control FIFO that cannot be opened refuses the session and is removed"
