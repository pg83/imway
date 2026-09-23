#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-file=2
# The screenshot file's first chunk of pixels does not go in.
set -euo pipefail
. "$(dirname "$0")/shot_file_fault_case.sh"
