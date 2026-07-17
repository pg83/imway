#!/usr/bin/env bash
# #2: the wl_keyboard keymap fd must be sealed (write + shrink).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT" || { echo "keymap fd is not sealed"; exit 1; }
echo "OK: keymap fd is sealed"
