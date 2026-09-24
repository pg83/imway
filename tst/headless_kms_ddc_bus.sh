#!/usr/bin/env bash
# imway-pre: mkdir -p noddc/card1-HDMI-A-1 busy/card0-HDMI-A-1/ddc/i2c-dev/power busy/card0-HDMI-A-1/ddc/i2c-dev/i2c-6 busy/card0-HDMI-A-1/ddc/i2c-dev/i2c-8
# Finding the connector's DDC/CI bus: a connector directory without a ddc
# link, and no drm class directory at all, leave the output without
# hardware brightness and never touch an i2c node. A ddc directory with
# something besides i2c nodes in it, and more than one of those, gives one
# bus: the first i2c node it lists.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

no_brightness() { # <what>
    boot_rc 0 "$1"
    boot_lacks "ddc/ci brightness on" "$1"
}

kms_boot IMWAY_SYSFS_DRM=./noddc IMWAY_FAKE_KMS_DDC=100 --
boot_lacks "fake-kms: ddc monitor" "connector without a ddc link"
no_brightness "connector without a ddc link"

kms_boot IMWAY_SYSFS_DRM=./nonexistent IMWAY_FAKE_KMS_DDC=100 --
boot_lacks "fake-kms: ddc monitor" "no drm class"
no_brightness "no drm class"

kms_boot IMWAY_SYSFS_DRM=./busy IMWAY_FAKE_KMS_DDC=100 --
boot_rc 0 "a busy ddc directory"
boot_has "ddc/ci brightness on /dev/i2c-[68], max 100" "a busy ddc directory"
boot_lacks "power" "a busy ddc directory"

expect_alive "the scenario's own compositor died"
echo "OK: no bus, no ddc/ci; one bus from a busy directory"
