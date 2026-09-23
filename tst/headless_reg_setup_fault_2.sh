#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=2
# The boot cannot create the scene render pass.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&renderPass)'
. "$(dirname "$0")/setup_fault_case.sh"
