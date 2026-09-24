#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-file=1 IMWAY_FAKE_KMS_NO_PRIME=1
# The screenshot file's header does not go in.
set -euo pipefail
. "$(dirname "$0")/shot_file_fault_case.sh"
