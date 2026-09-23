#!/usr/bin/env bash
# A v3 source that set its actions dragged onto a v1 data device: the
# offer hears no actions, the drop is a copy the source sees performed.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/dnd_version_case.sh"
