#!/usr/bin/env bash
# The top bar clock in the plain day.month formats; see clock_formats_case.sh.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

locale=false
. "$(dirname "$0")/clock_formats_case.sh"
