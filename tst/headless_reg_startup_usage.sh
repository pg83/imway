#!/usr/bin/env bash
# expect-startup-exit
# imway-args: --scale 0
# A command line the compositor cannot honour ends it before it starts, with
# the usage on record and the usage exit code, not a half-started session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

[[ "$IMWAY_RC" == 2 ]] || { echo "a bad --scale exited $IMWAY_RC, not 2"; cat "$IMWAY_LOG"; exit 1; }
in_log "usage:" || { echo "no usage on record"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a bad command line is refused with the usage"
