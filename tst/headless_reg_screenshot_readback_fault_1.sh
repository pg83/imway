#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-readback=1 IMWAY_FAKE_KMS_NO_PRIME=1
# The readback buffer's memory cannot be allocated.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/screenshot_readback_fault_case.sh"
