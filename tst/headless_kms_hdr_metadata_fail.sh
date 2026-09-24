#!/usr/bin/env bash
# imway-args: --hdr 300
# New HDR metadata the driver cannot take yet: a blob that fails to be
# created is reported and retried with the next frame; a blob created while
# no flip lands stays pending, never reaching the connector until a flip
# carries it, and a newer one replaces it while it waits. A night light at
# or above daylight is neutral.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "fake-kms: hdr metadata max_cll 300" || { echo "the boot metadata never reached the connector"; cat "$IMWAY_LOG"; exit 1; }

ctl "night 7000"
ctl "kms-fail-lookup createblob::0:1"
ctl "sdr-white 350"
await 100 in_log "cannot create updated HDR metadata blob" || { echo "the failed blob was not reported"; cat "$IMWAY_LOG"; exit 1; }
await 100 in_log "fake-kms: hdr metadata max_cll 350" || { echo "the metadata was not retried after the failed blob"; cat "$IMWAY_LOG"; exit 1; }

# no flip lands from here on: the next blob is created but waits
ctl "kms-fail-commit 16 100000"
ctl "sdr-white 420"
cll_is() { [[ "$(dump_field '^hdr' max_cll)" == "$1" ]]; }
await 100 cll_is 420 || { echo "the new metadata was not taken ($(dump_field '^hdr' max_cll))"; dump_state; exit 1; }

# a second change while the first still waits replaces it
ctl "sdr-white 440"
await 100 cll_is 440 || { echo "the replacing metadata was not taken ($(dump_field '^hdr' max_cll))"; dump_state; exit 1; }
sleep 0.5
! grep -q "fake-kms: hdr metadata max_cll 4[24]0" "$IMWAY_LOG" || { echo "metadata reached the connector without a flip"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died on refused HDR metadata"
echo "OK: refused HDR metadata is retried, unflipped metadata stays pending"
