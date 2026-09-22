#!/usr/bin/env bash
# A toplevel that sets a second pixel icon shows it, and the first one is
# retired from the window's icon store.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step
gen() { dump_field 'app_id=misc-icon ' icon_gen; }
has_icon() { local g; g=$(gen); [[ -n "$g" && "$g" != 0 && "$g" != "$1" ]]; }

start_client icon-twice
wait_client "step 1"
await 50 has_icon 0 || { echo "the first icon did not apply"; dump_state; exit 1; }
first=$(gen)
next
wait_client "step 2"
await 50 has_icon "$first" || { echo "the second icon did not replace the first (icon_gen=$(gen))"; dump_state; exit 1; }
echo "OK: a second pixel icon replaces the first"
