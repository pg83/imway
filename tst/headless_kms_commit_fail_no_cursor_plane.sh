#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_CURSOR_PLANE=1
# A refused page flip on a device without a cursor plane.
set -euo pipefail
. "$(dirname "$0")/kms_commit_fail_case.sh"
