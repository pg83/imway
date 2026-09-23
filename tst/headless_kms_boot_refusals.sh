#!/usr/bin/env bash
# A KMS driver that fails the backend where it has nothing to fall back on:
# refusing the universal-planes or atomic client capability, failing to list
# its resources or its planes, or failing to take the mode blob. Each ends
# the session at boot with the failed step on record and exit code 1, never
# a signal or a half-started session.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

refused() { # <rules> <what the log names>
    kms_boot IMWAY_FAKE_KMS_FAIL_LOOKUPS="$1" --
    boot_rc 1 "$1"
    boot_has "verify failed: $2" "$1"
    boot_lacks "clean exit after" "$1"
}

# the log names the failed check, macros expanded: universal planes is 2,
# atomic 3
refused clientcap::0:1 "drmSetClientCap(fd, 2, 1) == 0"
refused clientcap::1:1 "drmSetClientCap(fd, 3, 1) == 0"
refused resources::0:1 "res$"
refused planes::0:1 "planes$"
refused createblob::0:1 "drmModeCreatePropertyBlob(fd, &mode"

expect_alive "the scenario's own compositor died"
echo "OK: a driver failing the backend's unconditional steps ends the boot cleanly"
