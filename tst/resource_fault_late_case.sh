# The scenario's IMWAY_CHAOS makes one allocation per listed interface fail
# and resource_fault_late_runs names the client runs that reach them, one per
# line, in order: "bind IFACE" binds that global, any other word runs that
# request chain of client_resource_fault_late. A chain's own fault fires
# once every earlier one is spent. Each failed allocation must reach its
# client as a no_memory error while the compositor lives on.
client="$IMWAY_TESTS_BIN/client_resource_fault_late"

while read -r run; do
    [[ -n "$run" ]] || continue
    # shellcheck disable=SC2086
    "$client" $run || { echo "$run: the failed allocation did not reach the client as no_memory"; exit 1; }
    expect_alive "compositor died on a failed allocation in $run"
done <<<"$resource_fault_late_runs"

echo "OK: every failed allocation reached its client, compositor survived"
