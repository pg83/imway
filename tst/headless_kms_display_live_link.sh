#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_LINK_BPC=8
# The link negotiates 8 bits where 10 were asked for: the output reports
# the depth the link has, and a display setting that leaves the request as
# it was (a peak on an SDR output) modesets nothing: the depth is judged
# against what was asked, not against what the link came back with.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "requesting 10 bpc link" || { echo "no 10 bpc link was asked for"; cat "$IMWAY_LOG"; exit 1; }

remodesets() { grep -c "display settings changed, remodeset" "$IMWAY_LOG" || true; }

# a dump is answered after every command before it
dump_state >/dev/null
ctl "set display.peak_nits 700"
dump_state >/dev/null
[[ "$(remodesets)" == 0 ]] || { echo "a setting that asks for nothing new modeset the display"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a shallow link"
echo "OK: a link shallower than asked for does not turn every setting into a modeset"
