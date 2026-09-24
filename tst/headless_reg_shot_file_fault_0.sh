#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=shot-file=0 IMWAY_FAKE_KMS_NO_PRIME=1
# The screenshot's memfd cannot be made.
set -euo pipefail
. "$(dirname "$0")/shot_file_fault_case.sh"
