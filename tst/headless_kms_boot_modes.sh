#!/usr/bin/env bash
# Which connector and mode the output boots on. A display that is unplugged
# or offers no mode at all is no desktop connector, and with no other one
# the boot ends on record. A mode list with none preferred boots its first
# mode. An asked-for mode is matched on size and, when given, refresh: a
# size offered only at another refresh is refused, one offered at the rate
# asked for is taken even behind a same-width mode of another height, and
# one without a rate takes the first of that size.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot IMWAY_FAKE_KMS_CONNECTED=0 --
boot_rc 1 "unplugged at boot"
boot_has "verify failed: conn$" "unplugged at boot"
boot_lacks "clean exit after" "unplugged at boot"

kms_boot IMWAY_FAKE_KMS_MODES=4 --
boot_rc 1 "no modes at boot"
boot_has "verify failed: conn$" "no modes at boot"
boot_lacks "clean exit after" "no modes at boot"

# forty modes, none preferred: 1920x1080@60, 1280x800@50, 1280x720@60, ...
kms_boot IMWAY_FAKE_KMS_MODES=5 --
boot_rc 0 "no preferred mode"
boot_has "kms output: 1920x1080@60" "no preferred mode"
boot_has "clean exit after" "no preferred mode"

kms_boot IMWAY_FAKE_KMS_MODES=5 -- --mode 1280x800@60
boot_rc 1 "a size only at another rate"
boot_has "mode 1280x800@60 not offered by the connector" "a size only at another rate"
boot_lacks "clean exit after" "a size only at another rate"

kms_boot IMWAY_FAKE_KMS_MODES=5 -- --mode 1280x720@60
boot_rc 0 "a mode behind a same-width one"
boot_has "kms output: 1280x720@60" "a mode behind a same-width one"
boot_has "clean exit after" "a mode behind a same-width one"

kms_boot IMWAY_FAKE_KMS_MODES=5 -- --mode 1920x1080
boot_rc 0 "a mode without a rate"
boot_has "kms output: 1920x1080@60" "a mode without a rate"
boot_has "clean exit after" "a mode without a rate"

expect_alive "the scenario's own compositor died"
echo "OK: the boot takes the connector and mode the display offers, or refuses"
