# The output moves to a new mode and rebuilding the renderer's output-sized
# targets for it fails at the step the scenario's IMWAY_CHAOS=output-target=K
# names: the boot builds them with $boot_calls calls, the rebuild's are the
# ones after, and $boot_line is the backend the count assumes. There is no
# smaller frame to fall back to: the session ends, and the teardown must not
# trip over the old targets the rebuild had already destroyed.
in_log "$boot_line" || { echo "not the backend the fault count assumes ($boot_line)"; cat "$IMWAY_LOG"; exit 1; }
in_log "kms output: 1280x800@60" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

ctl "kms-connector 0"
await 50 in_log "connector disconnected" || { echo "disconnect unnoticed"; exit 1; }
ctl "kms-modes 1"
ctl "kms-connector 1"
exec 3>&-

compositor_gone() {
    # the harness has not reaped the compositor yet, so kill -0 would still
    # succeed on the zombie — read the real process state instead
    local st
    st=$(awk '{print $3}' "/proc/$IMWAY_PID/stat" 2>/dev/null) || return 0
    [[ -z "$st" || "$st" == Z ]]
}
await 100 compositor_gone || { echo "the compositor outlived targets it could not build"; cat "$IMWAY_LOG"; exit 1; }

in_log "imway: fatal" || { echo "the failure was not reported"; cat "$IMWAY_LOG"; exit 1; }
# $fault_call, when set, is the call the fault is meant to hit
if [[ -n "${fault_call:-}" ]]; then
    grep "imway: fatal" "$IMWAY_LOG" | grep -qF "$fault_call" || { echo "the fault did not hit $fault_call"; grep "imway: fatal" "$IMWAY_LOG"; exit 1; }
fi
! in_log "fatal signal" || { echo "the teardown faulted on the half-rebuilt targets"; cat "$IMWAY_LOG"; exit 1; }

echo "OK: a failed output-target rebuild ends the session cleanly"
