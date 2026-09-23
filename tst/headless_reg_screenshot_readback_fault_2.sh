#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-readback=2
# The readback buffer's memory cannot be bound to it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/screenshot_readback_fault_case.sh"
