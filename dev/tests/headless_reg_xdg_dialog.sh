#!/usr/bin/env bash
# xdg-dialog: set_modal / unset_modal must toggle the toplevel modal state.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "modal"
sleep 0.2
m=$(dump_field 'app_id=xdg-dialog' modal)
[[ "${m:-0}" -eq 1 ]] || { echo "modal not set (got '$m')"; exit 1; }

ctl "key 57 press"
ctl "key 57 release"
wait_client "modeless"
sleep 0.2
m=$(dump_field 'app_id=xdg-dialog' modal)
[[ "${m:-1}" -eq 0 ]] || { echo "modal not cleared (got '$m')"; exit 1; }

echo "OK: xdg-dialog toggles modality"
