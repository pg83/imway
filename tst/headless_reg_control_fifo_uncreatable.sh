#!/usr/bin/env bash
# imway-args: --control no-such-dir/ctl
# expect-startup-exit
# A control FIFO asked for in a directory that does not exist cannot be
# made: the compositor refuses to start, naming the failed mkfifo, with an
# exit code rather than a signal.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" -eq 1 ]] || { echo "an uncreatable control FIFO exited $IMWAY_RC"; cat "$IMWAY_LOG"; exit 1; }
grep "imway: fatal" "$IMWAY_LOG" | grep -qF "mkfifo(path.cStr(), 0600) == 0" || { echo "the refusal does not name the mkfifo"; cat "$IMWAY_LOG"; exit 1; }
! in_log "control FIFO:" || { echo "a control FIFO was reported up"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e no-such-dir ]] || { echo "the missing directory appeared"; exit 1; }
echo "OK: a control FIFO that cannot be made refuses the session"
