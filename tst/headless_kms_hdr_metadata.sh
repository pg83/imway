#!/usr/bin/env bash
# imway-args: --hdr 300
# The HDR metadata follows the content on a live HDR link: moving SDR white
# changes the content light level, the new infoframe blob rides the next
# page flip, and settings that change nothing (a zero or unchanged white)
# send nothing new.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "HDR output: BT.2020 + PQ" || { echo "no HDR boot"; cat "$IMWAY_LOG"; exit 1; }

cll() { dump_field '^hdr' max_cll; }
sent() { grep -c "fake-kms: hdr metadata max_cll" "$IMWAY_LOG" || true; }

await 100 in_log "fake-kms: hdr metadata max_cll 300" || { echo "the boot metadata never reached the connector"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(cll)" == 300 ]] || { echo "boot max_cll is $(cll), expected 300"; dump_state; exit 1; }

ctl "sdr-white 250"
await 100 in_log "fake-kms: hdr metadata max_cll 250" || { echo "the new metadata did not ride a flip"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(cll)" == 250 ]] || { echo "max_cll is $(cll) after the change, expected 250"; dump_state; exit 1; }

n=$(sent)
ctl "sdr-white 0"
ctl "sdr-white 250"
ctl "key 2 press"; ctl "key 2 release"
sleep 0.5
[[ "$(sent)" == "$n" ]] || { echo "an unchanged white sent new metadata"; grep "fake-kms: hdr" "$IMWAY_LOG"; exit 1; }
[[ "$(cll)" == 250 ]] || { echo "max_cll moved to $(cll) on a no-op"; dump_state; exit 1; }

# and back up: every change is a fresh blob, the old one retired
ctl "sdr-white 400"
await 100 in_log "fake-kms: hdr metadata max_cll 400" || { echo "the second change did not ride a flip"; cat "$IMWAY_LOG"; exit 1; }

# the infoframe carries light levels in 16 bits: a display said to peak
# past that, with SDR white up there too, gets the most the field holds,
# not a wrapped-around level
kms_boot -- --hdr 70000 --hdr-peak 100000
boot_rc 0 "light levels past 16 bits"
boot_has "fake-kms: hdr metadata max_cll 65535$" "light levels past 16 bits"
boot_lacks "fake-kms: hdr metadata max_cll [0-9]\{1,4\}$" "light levels past 16 bits"
boot_has "clean exit after" "light levels past 16 bits"

expect_alive "compositor died updating HDR metadata"
echo "OK: HDR metadata follows SDR white on the next flip, no-ops send nothing"
