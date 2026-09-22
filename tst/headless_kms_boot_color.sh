#!/usr/bin/env bash
# The RGB range the connector is told follows --rgb-range, whichever
# spelling the driver uses for "limited".
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot -- --rgb-range full
boot_rc 0 "full range"
boot_has "fake-kms: Broadcast RGB = 1"

kms_boot -- --rgb-range limited
boot_rc 0 "limited range"
boot_has "fake-kms: Broadcast RGB = 2"

kms_boot IMWAY_FAKE_KMS_LEGACY_RANGE=1 -- --rgb-range limited
boot_rc 0 "limited range, legacy spelling"
boot_has "fake-kms: Broadcast RGB = 2"

expect_alive "the scenario's own compositor died"
echo "OK: the RGB range reaches the connector"
