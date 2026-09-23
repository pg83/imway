#!/usr/bin/env bash
# No pipe to drive at boot: the desktop connector's encoder cannot be read,
# the encoder reaches no crtc, no plane says it is primary, the connector
# itself cannot be read, or the requested mode does not parse. The session
# ends at boot with the missing piece on record and exit code 1.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

refused() { # <what the log names> <what> [VAR=value...] -- [imway args...]
    local names=$1 what=$2
    shift 2
    kms_boot "$@"
    boot_rc 1 "$what"
    boot_has "verify failed: $names" "$what"
    boot_lacks "clean exit after" "$what"
}

refused "enc$" "encoder unreadable" IMWAY_FAKE_KMS_FAIL_LOOKUPS=encoder:102::-1 --
refused "crtcId$" "no crtc for the encoder" IMWAY_FAKE_KMS_UNBOUND=1 IMWAY_FAKE_KMS_NO_CRTC=1 --
refused "planeId$" "no primary plane" IMWAY_FAKE_KMS_DROP_PROPS=type --
refused "conn$" "connector unreadable" IMWAY_FAKE_KMS_FAIL_LOOKUPS=connector:101::-1 --
refused "want.parse(modeStr)$" "unparseable mode" -- --mode sideways

expect_alive "the scenario's own compositor died"
echo "OK: a boot without a pipe to drive ends cleanly"
