#!/usr/bin/env bash
# imway-pre: mkdir -p drm/card0-HDMI-A-1/ddc/i2c-dev/i2c-7 drm/card0-DP-1/ddc/i2c-dev/i2c-3 drm/card0 && touch drm/version
# Monitors on the connector's DDC/CI bus that do not play along: one with
# DDC/CI switched off never answers the brightness query, one reports a
# zero range, and a bus node that cannot be opened has no monitor at all.
# Each leaves the output without hardware brightness.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

no_brightness() { # <what>
    boot_rc 0 "$1"
    boot_lacks "ddc/ci brightness on" "$1"
}

kms_boot IMWAY_SYSFS_DRM=./drm IMWAY_FAKE_KMS_DDC=silent --
boot_has "fake-kms: ddc monitor on /dev/i2c-7" "ddc/ci switched off"
no_brightness "ddc/ci switched off"

kms_boot IMWAY_SYSFS_DRM=./drm IMWAY_FAKE_KMS_DDC=0 --
boot_has "fake-kms: ddc monitor on /dev/i2c-7" "zero range"
no_brightness "zero range"

kms_boot IMWAY_SYSFS_DRM=./drm IMWAY_FAKE_KMS_DDC= --
boot_lacks "fake-kms: ddc monitor" "no i2c device"
no_brightness "no i2c device"

expect_alive "the scenario's own compositor died"
echo "OK: a monitor that does not answer ddc/ci gets no hardware brightness"
