# HDR switched on live where the output cannot carry it ($reason, what the
# scenario's emulator lacks): the refusal is logged, the output stays SDR
# and frames keep coming. Sourced by headless_kms_display_live_hdr_*.sh.
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "set display.hdr_enabled true"
await 50 in_log "HDR unsupported here, staying SDR" || { echo "the refused HDR was not reported"; cat "$IMWAY_LOG"; exit 1; }
in_log "$reason" || { echo "the refusal does not say why: $reason"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(dump_field '^hdr ' metadata)" == 0 ]] || { echo "the output reports HDR metadata"; dump_state; exit 1; }
"$IMWAY_TESTS_BIN/client_health_probe" || { echo "the output stopped presenting"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died refusing HDR"
echo "OK: live HDR the output cannot carry is refused, the output stays SDR"
