# A finished frame's fence goes wrong, well after boot (the scenario's
# IMWAY_CHAOS picks the call and the result; each finished frame spends two,
# its status and its reset). A failed fence status or reset is a lost
# device: the loop stops and the compositor tears down with that frame's
# resources still held, which must not fault. A wait that times out is a hung
# GPU: the compositor leaves on the spot with status 1, unwinding nothing.
# $fault_log is the line the failure reports, $exit_status the status the
# compositor leaves with.
compositor_gone() {
    # the harness has not reaped the compositor yet, so kill -0 would still
    # succeed on the zombie — read the real process state instead
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}

# every pointer move is a frame; keep moving until the faulted one. The
# FIFO loses its reader when the compositor goes: a write then fails
# instead of killing the scenario
trap '' PIPE
for i in $(seq 1 200); do
    compositor_gone && break
    ctl "motion $((100 + i * 4)) $((100 + i))" 2>/dev/null || break
    sleep 0.05
done
exec 3>&-

await 100 compositor_gone || { echo "the compositor outlived a failed frame fence"; cat "$IMWAY_LOG"; exit 1; }
in_log "$fault_log" || { echo "the fence failure was not reported: $fault_log"; cat "$IMWAY_LOG"; exit 1; }
! in_log "fatal signal" || { echo "teardown faulted after the fence failure"; cat "$IMWAY_LOG"; exit 1; }

if [[ "$exit_status" == 0 ]]; then
    in_log "clean exit after" || { echo "the lost device did not end the session through the loop"; cat "$IMWAY_LOG"; exit 1; }
else
    ! in_log "clean exit after" || { echo "a hung GPU unwound the session"; cat "$IMWAY_LOG"; exit 1; }
fi

echo "OK: a failed frame fence ends the session as it should ($fault_log)"
