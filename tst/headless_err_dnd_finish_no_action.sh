#!/usr/bin/env bash
# A drop from a source that never set its actions is an unnegotiated copy:
# the offer got no action, so finish is an invalid_finish error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_finished_case.sh"
