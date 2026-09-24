#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_NO_PRIME=1
# The dmabuf case on a dumb-buffer display: the frame reaches the display
# through a CPU copy, with no present fence to hand the flip, and the
# fence the client's dma-buf gets for its readers is the only one the
# frame exports.
set -euo pipefail
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_dmabuf"
. "$(dirname "$0")/dmabuf_case.sh"
