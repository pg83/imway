# A Vulkan call building the session's once-only GPU objects at boot fails
# (the scenario's IMWAY_CHAOS=setup=K names which; the boot on the KMS
# emulator makes its calls in a fixed order). Nothing can run
# without them: the compositor refuses to start, with an exit code rather
# than a signal (the harness judges that), the failed call on record
# ($fault_call) and nothing half-built faulting on the way out.
[[ "$IMWAY_RC" != 0 ]] || { echo "the compositor exited 0 though $fault_call failed"; cat "$IMWAY_LOG"; exit 1; }
grep "imway: fatal" "$IMWAY_LOG" | grep -qF "$fault_call" || {
    echo "the refusal does not name $fault_call"
    cat "$IMWAY_LOG"
    exit 1
}
! in_log "fatal signal" || { echo "the teardown faulted after $fault_call failed"; cat "$IMWAY_LOG"; exit 1; }
echo "OK: a failed $fault_call refuses the session cleanly"
