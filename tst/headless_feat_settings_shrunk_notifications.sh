#!/usr/bin/env bash
# The settings dialog squeezed to its smallest size on the notifications page: the
# page's table of notification rule rows is not drawn and the dialog survives.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

SHRUNK_PAGE=7
. "$(dirname "$0")/settings_shrunk_case.sh"
echo "OK: the notifications page squeezed out draws nothing and the dialog survives"
