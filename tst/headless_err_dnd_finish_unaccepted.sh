#!/usr/bin/env bash
# finish after the drop was un-accepted with a null mime type is an
# invalid_finish error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_finished_case.sh"
