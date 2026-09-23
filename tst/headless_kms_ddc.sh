#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_DDC=100 IMWAY_SYSFS_DRM=./drm
# imway-args: --device auto
# imway-pre: mkdir -p drm/card0-HDMI-A-1/ddc/i2c-dev/i2c-7 drm/card0-DP-1/ddc/i2c-dev/i2c-3 drm/card0 drm/card-HDMI-A-1 drm/card0xHDMI-A-1 && touch drm/version
# The brightness of an external monitor over DDC/CI: the connector's own
# i2c bus is found in sysfs (never a neighbour's, never a prefix match),
# the monitor's VCP 0x10 range is read at boot, and the brightness keys
# reach it as coalesced Set VCP writes. A bus with no device on the
# monitor's address leaves the output without hardware brightness, and an
# HDR output pins the monitor at full.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "fake-kms: ddc monitor on /dev/i2c-7" || { echo "the connector's own bus was not opened"; cat "$IMWAY_LOG"; exit 1; }
in_log "ddc/ci brightness on /dev/i2c-7, max 100" || { echo "the monitor's range was not read"; cat "$IMWAY_LOG"; exit 1; }
! in_log "i2c-3" || { echo "a neighbouring connector's bus was touched"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.osd_seconds 5"
await 100 in_log "control: set display.osd_seconds" || { echo "settings are not reachable"; exit 1; }

# half of 100 at boot, one 5% step up
ctl "key 225 press"; ctl "key 225 release" # KEY_BRIGHTNESSUP
await 50 in_log "fake-kms: ddc set vcp 16 = 55" || { echo "the brightness key did not reach the monitor"; cat "$IMWAY_LOG"; exit 1; }

# three quick steps down coalesce behind the bus's write spacing, and the
# last value always lands
for _ in 1 2 3; do
    ctl "key 224 press"; ctl "key 224 release" # KEY_BRIGHTNESSDOWN
done
await 50 in_log "fake-kms: ddc set vcp 16 = 40" || { echo "the last step did not reach the monitor"; cat "$IMWAY_LOG"; exit 1; }

no_brightness() { # <what>
    boot_rc 0 "$1"
    boot_lacks "ddc/ci brightness on" "$1"
}

kms_boot IMWAY_FAKE_KMS_DDC=absent --
boot_has "fake-kms: ddc monitor on /dev/i2c-7" "no device on the address"
no_brightness "no device on the address"

kms_boot -- --hdr 300
boot_has "ddc/ci brightness on /dev/i2c-7, max 100" "hdr"
boot_has "HDR pins hardware brightness to full" "hdr"

expect_alive "compositor died driving ddc/ci"
echo "OK: ddc/ci brightness on the connector's own bus, refusals leave it off"
