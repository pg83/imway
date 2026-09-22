# The device runs out of memory for one GPU resource sized by a client's
# wl_shm buffer (IMWAY_CHAOS=client-texture=K lets K such allocations
# through and fails the next): the owner is disconnected with no_memory as
# a render fault, and the session goes on rendering for everyone else.
# wl_shm stays on the CPU copy, so the allocations are the upload buffer's
# (K 0-3) and then the texture's (K 4-6); $fault_log is what the failed
# step reports, empty when it reports nothing of its own.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_render_fault"
start_client
wait_client "render fault ready"

await 100 in_log "render fault, disconnecting render-fault-victim" || {
    echo "the owner of the failed allocation was not disconnected"
    cat "$IMWAY_LOG"
    exit 1
}

if [[ -n "$fault_log" ]] && ! in_log "$fault_log"; then
    echo "the failed allocation did not report: $fault_log"
    cat "$IMWAY_LOG"
    exit 1
fi

expect_client_ok "the faulted client did not see its disconnect"

"$(dirname "$IMWAY_CLIENT")/client_health_probe" || {
    echo "the output stopped presenting after the render fault"
    cat "$IMWAY_LOG"
    exit 1
}

expect_alive "compositor died on a failed client allocation"
echo "OK: a failed client-sized allocation faults its owner only"
