#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_LINK_BPC=8
# imway-args: --device auto --hdr 300
# The connector accepts the HDR configuration but the link only negotiates
# 8 bpc: the feedback path notices after the first flip and degrades the
# session to SDR instead of showing banded PQ.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

await 100 in_log "HDR link degraded to 8 bpc; falling back to SDR" || {
    echo "low link bpc went unnoticed"
    cat "$IMWAY_LOG"
    exit 1
}
in_log "kms output" || { echo "session did not light up"; cat "$IMWAY_LOG"; exit 1; }

hdr() { dump_field '^hdr' metadata; }

sdr() { [[ "$(hdr)" == "0" ]]; }

await 50 sdr || { echo "hdr metadata still set after the degrade"; dump_state; exit 1; }

flips() { dump_field '^kms' flips; }

f0=$(flips)

advanced() { [[ "$(flips)" -gt "$f0" ]]; }

ctl "key 2 press"; ctl "key 2 release"
await 100 advanced || { echo "no flips after the SDR fallback"; exit 1; }

expect_alive "compositor died on a degraded link"
echo "OK: an 8 bpc link degrades HDR to SDR, the session lives"
