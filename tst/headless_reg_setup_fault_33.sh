#!/usr/bin/env bash
# expect-startup-exit
# imway-env: IMWAY_CHAOS=setup=33
# The boot cannot create the output pipeline.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_call='&outputPipeline)'
. "$(dirname "$0")/setup_fault_case.sh"
