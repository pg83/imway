#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_FAKE_CURSOR_PLANE=1 IMWAY_CHAOS=setup=26
# The boot cannot create the sampler.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&sampler)'
. "$(dirname "$0")/setup_fault_case.sh"
