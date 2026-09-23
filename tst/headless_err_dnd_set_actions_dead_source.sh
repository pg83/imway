#!/usr/bin/env bash
# set_actions on an offer whose drag source was destroyed after the drop is
# an invalid_offer error.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_finished_case.sh"
