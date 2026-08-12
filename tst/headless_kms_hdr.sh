#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1
# imway-args: --device auto --hdr 300
# The positive HDR path: the synthetic EDID advertises PQ + BT.2020 with a
# luminance range, the connector takes the color configuration and the
# session runs BT.2020 + PQ end to end.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output" || { echo "session did not light up"; cat "$IMWAY_LOG"; exit 1; }
! in_log "falling back to SDR" || { echo "HDR degraded unexpectedly"; cat "$IMWAY_LOG"; exit 1; }

# the luminance range must come from the EDID, not the built-in fallbacks
! in_log "EDID unavailable or invalid" || { echo "EDID not parsed"; cat "$IMWAY_LOG"; exit 1; }
! in_log "using 1000 nit fallback" || { echo "EDID luminance not read"; cat "$IMWAY_LOG"; exit 1; }

hdr() { dump_field '^hdr' metadata; }

[[ "$(hdr)" == "1" ]] || { echo "no hdr metadata in the state dump"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips under HDR"; exit 1; }

expect_alive "compositor died under HDR"
echo "OK: EDID-backed HDR boots and flips"
