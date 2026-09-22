#!/usr/bin/env bash
# imway-pre: mkdir -p noddc/card1-HDMI-A-1
# Finding the connector's DDC/CI bus: a connector directory without a ddc
# link, and no drm class directory at all, leave the output without
# hardware brightness and never touch an i2c node.
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

expect_alive "the scenario's own compositor died"
echo "OK: no bus, no ddc/ci"
