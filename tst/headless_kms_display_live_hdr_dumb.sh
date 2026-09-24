#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_PRIME=1
# The display takes dumb buffers only: nothing scans out of a swapchain,
# let alone a 10-bit one.
set -euo pipefail
reason="imway: hdr: the scanout is not 10-bit"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
