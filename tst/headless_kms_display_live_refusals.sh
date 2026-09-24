#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_MAX_BPC=10 IMWAY_FAKE_KMS_DROP_PROPS="Broadcast RGB"
# Display settings the connector cannot carry, asked for live: a link
# deeper than its max bpc range and an RGB range on a connector without
# one are refused with a log line, and the output stays as it was.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.bpc 12"
await 50 in_log "imway: the connector cannot carry 12 bpc" || { echo "a 12 bpc link past the range was not refused"; cat "$IMWAY_LOG"; exit 1; }
ctl "set display.bpc 0"

ctl "set display.range 2"
await 50 in_log "imway: connector cannot select requested RGB range" || { echo "a range without the property was not refused"; cat "$IMWAY_LOG"; exit 1; }
! in_log "display settings changed, remodeset" || { echo "a refused setting modeset the display"; cat "$IMWAY_LOG"; exit 1; }

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died refusing display settings"
echo "OK: display settings the connector cannot carry are refused and change nothing"
