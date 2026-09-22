#!/usr/bin/env bash
# A dialog whose xdg_dialog is destroyed stops being modal; the toplevel
# stays mapped.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

start_client dialog
field() { dump_field 'app_id=misc-dialog ' "$1"; }
modal_is() { [[ "$(field modal)" == "$1" ]]; }
wait_client "step 1"
await 50 modal_is 1 || { echo "set_modal did not apply"; dump_state; exit 1; }
next
wait_client "step 2"
await 50 modal_is 0 || { echo "the destroyed xdg_dialog left the toplevel modal"; dump_state; exit 1; }
[[ "$(field mapped)" == 1 ]] || { echo "the toplevel went with its xdg_dialog"; dump_state; exit 1; }
echo "OK: destroying the xdg_dialog ends modality only"
