#!/usr/bin/env bash
# The drag source destroyed after the drop: a receive on its offer gets a
# pipe that ends at once, an accept is ignored, and finish is an
# invalid_finish error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_finished_case.sh"
