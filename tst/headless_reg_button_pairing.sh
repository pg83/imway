#!/usr/bin/env bash
# Button press/release pairing. Phase A: press over client content, drag onto
# the empty desktop, release — the implicit grab must deliver the release and
# keep motion flowing. Phase B: right-press on the SSD title bar, release over
# client content — no orphan button events may leak to the client.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "pairing ready"
sleep 0.5
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

x=$(dump_field 'app_id=pairing' x); y=$(dump_field 'app_id=pairing' y)
w=$(dump_field 'app_id=pairing' w)
imgx=$(dump_field 'app_id=pairing' imgx); imgy=$(dump_field 'app_id=pairing' imgy)
cw=$(dump_field 'app_id=pairing' client_w); ch=$(dump_field 'app_id=pairing' client_h)
cx=$((imgx + cw / 2)); cy=$((imgy + ch / 2))
echo "window at $x,$y w=$w content at $imgx,$imgy ${cw}x${ch}"

# phase A: press over content, drag out to the empty desktop, release there
ctl "motion $cx $cy"
sleep 0.3                     # a frame computes hover before the press
ctl "button left press"
sleep 0.2
ctl "motion 1150 750"
sleep 0.2
ctl "motion 1200 770"
sleep 0.2
ctl "button left release"
wait_client "pairA ok"

# phase B: right-press on the title bar (between window top and content top),
# release over the content
tx=$((x + w / 2)); ty=$(((y + imgy) / 2))
ctl "motion $tx $ty"
sleep 0.3                     # a frame must see the chrome hover
ctl "button right press"
sleep 0.2
ctl "motion $cx $cy"
sleep 0.3
ctl "button right release"
sleep 0.3
ctl "key 50 press"            # KEY_M: the phase-B sentinel
ctl "key 50 release"

expect_client_ok "button pairing broken"
echo "OK: implicit grab paired press/release; chrome clicks leaked nothing"
