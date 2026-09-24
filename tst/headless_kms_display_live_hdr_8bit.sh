#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_10BIT=1
# The plane has no 10-bit format: the buffers scanning out are 8-bit.
set -euo pipefail
reason="imway: hdr: the scanout is not 10-bit"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
