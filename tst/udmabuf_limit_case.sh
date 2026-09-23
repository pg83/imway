# The udmabuf module's size cap as sysfs states it (the test build reads a
# staged file, IMWAY_SYSFS_UDMABUF_LIMIT): a pool bigger than the cap is
# never handed to udmabuf and goes through the CPU copy; a cap that is
# missing, empty or beyond counting caps nothing. Sourced by
# headless_reg_udmabuf_limit_*.sh with $expect, the backend both 3 MiB
# sealed commits must take: cpu or udmabuf-buffer.
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_shm_external_host"

if ! in_log "wl_shm gates image=[01] buffer=1"; then
    echo "SKIP: no usable /dev/udmabuf, the cap has nothing to hold back"
    exit 127
fi

start_client
wait_client "second sealed buffer committed"

# the toplevel's first buffer is a plain pool: when a frame uploads it
# (not always, the sealed commit can replace it first) it goes to cpu.
# Two frames after the client's last commit, both sealed pools have been
# prepared; a plain pool never goes to udmabuf, so two udmabuf-buffer lines
# are both sealed pools, and on the cpu side no udmabuf-buffer line at all
screenshot "$XDG_RUNTIME_DIR/_a.ppm"
screenshot "$XDG_RUNTIME_DIR/_b.ppm"
backend_lines() { grep -c "wl_shm backend $1" "$IMWAY_LOG" || true; }
carried() { [[ $(backend_lines "$expect") -ge 2 ]]; }

await 100 carried || {
    if [[ $expect == udmabuf-buffer ]] && in_log "disabling wl_shm UDMABUF"; then
        echo "SKIP: the udmabuf import itself fails on this host, past the cap"
        exit 127
    fi
    echo "the 3 MiB pools did not both go to $expect"
    grep "wl_shm" "$IMWAY_LOG"
    exit 1
}

if [[ $expect == cpu ]] && { in_log "disabling wl_shm UDMABUF" || [[ $(backend_lines udmabuf) -gt 0 ]]; }; then
    echo "a pool over the cap was handed to udmabuf anyway"
    grep "wl_shm" "$IMWAY_LOG"
    exit 1
fi

expect_alive "compositor died reading the udmabuf cap"
echo "OK: the udmabuf cap as staged sends the pools to $expect"
