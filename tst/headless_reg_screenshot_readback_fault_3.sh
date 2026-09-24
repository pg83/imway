#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-readback=3 IMWAY_FAKE_KMS_NO_PRIME=1
# The readback buffer's memory cannot be mapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/screenshot_readback_fault_case.sh"
