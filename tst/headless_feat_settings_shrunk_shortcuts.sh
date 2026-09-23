#!/usr/bin/env bash
# The settings dialog squeezed to its smallest size on the shortcuts page: the
# page's table of shortcut rows is not drawn and the dialog survives.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

SHRUNK_PAGE=6
. "$(dirname "$0")/settings_shrunk_case.sh"
echo "OK: the shortcuts page squeezed out draws nothing and the dialog survives"
