#!/usr/bin/env bash
# A source already used for a drag, handed to set_selection, is a used_source
# error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_error_case.sh"
