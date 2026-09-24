#!/usr/bin/env bash
# The display settings reach the connector live, each with a modeset on the
# buffers already scanning out: the RGB range as each of its values, the
# link depth; a setting that leaves the color state as it was (a peak on an
# SDR output) modesets nothing. A metadata blob the driver cannot make
# keeps the output as it was, and the next try goes through.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

remodesets() { grep -c "display settings changed, remodeset" "$IMWAY_LOG" || true; }
hdr_is() { [[ "$(dump_field '^hdr ' metadata)" == "$1" ]]; }

ctl "set display.range 2"
await 50 in_log "fake-kms: Broadcast RGB = 2" || { echo "the limited range did not reach the connector"; cat "$IMWAY_LOG"; exit 1; }
ctl "set display.range 1"
await 50 in_log "fake-kms: Broadcast RGB = 1" || { echo "the full range did not reach the connector"; cat "$IMWAY_LOG"; exit 1; }
ctl "set display.range 0"
await 50 in_log "fake-kms: Broadcast RGB = 0" || { echo "the automatic range did not reach the connector"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.bpc 8"
await 50 in_log "fake-kms: max bpc = 8" || { echo "an 8 bpc link was not asked for"; cat "$IMWAY_LOG"; exit 1; }

n=$(remodesets)
ctl "set display.peak_nits 700"
await 20 in_log "control: set display.peak_nits" || { echo "settings are not reachable"; exit 1; }
ctl "frame"
[[ "$(remodesets)" == "$n" ]] || { echo "a peak on an SDR output modeset the display"; exit 1; }

ctl "kms-fail-lookup createblob::0:1"
ctl "set display.hdr_enabled true"
await 50 in_log "cannot create the HDR metadata blob, the output stays as it was" || { echo "the refused blob was not reported"; cat "$IMWAY_LOG"; exit 1; }
ctl "frame"
hdr_is 0 || { echo "the output went HDR without its metadata"; dump_state; exit 1; }

# the setting is still on: the next display setting takes it through
ctl "set display.peak_nits 800"
await 100 hdr_is 1 || { echo "HDR did not come up once the blob could be made"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died applying display settings"
echo "OK: display settings modeset the connector live, a refused blob changes nothing"
