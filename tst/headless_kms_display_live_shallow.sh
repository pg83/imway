#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_MAX_BPC=8
# A connector whose max bpc stops at 8 under a 10-bit framebuffer: the link
# asked for is 8 bits at boot, and a display setting that changes nothing
# else judges the depth the same way, so it modesets nothing.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "requesting 8 bpc link for the 10-bit framebuffer" || { echo "the link asked for is not the connector's 8 bits"; cat "$IMWAY_LOG"; exit 1; }

remodesets() { grep -c "display settings changed, remodeset" "$IMWAY_LOG" || true; }

# a dump is answered after every command before it
dump_state >/dev/null
ctl "set display.peak_nits 700"
dump_state >/dev/null
[[ "$(remodesets)" == 0 ]] || { echo "a setting that asks for nothing new modeset the display"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on a shallow connector"
echo "OK: an 8 bpc connector gets an 8 bit link, live settings judge it alike"
