#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_EDID=garbage
# HDR switched on live on a display whose EDID says nothing readable: with
# no capabilities to hold it against, the output takes the setting at its
# word and goes HDR, metadata and all.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }
ctl "set display.hdr_enabled true"
await 100 hdr_is 1 || { echo "HDR did not come up on a display of unknown capabilities"; cat "$IMWAY_LOG"; exit 1; }
! in_log "HDR unsupported here" || { echo "HDR was refused for want of an EDID"; cat "$IMWAY_LOG"; exit 1; }

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died going HDR on an unreadable EDID"
echo "OK: live HDR on a display of unknown capabilities goes through"
