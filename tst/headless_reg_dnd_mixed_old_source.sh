#!/usr/bin/env bash
# A v1 source dragged onto a v3 data device: the offer hears the source
# offers no actions and picks none, the source hears nothing of it, and
# the drop is a copy.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_version_case.sh"
