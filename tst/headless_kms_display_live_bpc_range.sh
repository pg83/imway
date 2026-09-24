#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_MIN_BPC=8 IMWAY_FAKE_KMS_MAX_BPC=10
# Link depths outside the connector's max bpc range, asked for live: 12 bpc
# above a range that ends at 10 and 6 below one that starts at 8 are both
# refused with a log line, and the output stays as it was.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.bpc 12"
await 50 in_log "imway: the connector cannot carry 12 bpc" || { echo "12 bpc above the connector's range was not refused"; cat "$IMWAY_LOG"; exit 1; }
ctl "set display.bpc 6"
await 50 in_log "imway: the connector cannot carry 6 bpc" || { echo "6 bpc below the connector's range was not refused"; cat "$IMWAY_LOG"; exit 1; }
! in_log "display settings changed, remodeset" || { echo "a refused depth modeset the display"; cat "$IMWAY_LOG"; exit 1; }

"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }
expect_alive "compositor died refusing link depths"
echo "OK: link depths outside the connector's range are refused and change nothing"
