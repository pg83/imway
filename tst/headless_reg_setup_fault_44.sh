#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=44
# The boot cannot create the screenshot capture's command pool.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&commandPool)'
. "$(dirname "$0")/setup_fault_case.sh"
