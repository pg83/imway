#!/usr/bin/env bash
# #F-14 regression: destroying the input method before its popup surface must
# not dangle the popup on freed memory (the popup back-pointer has to be
# severed just like the keyboard grab). The client destroys the input method
# first, then the popup; the compositor must survive both.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "input-popup-destroy done"
expect_alive
echo "OK: input method destroyed before its popup, compositor survived"
