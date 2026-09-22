# The scenario's IMWAY_CHAOS makes one allocation per listed interface fail,
# and resource_fault_modes names the request chains that reach them, in
# order: each chain's own fault fires once every earlier one is spent. A
# failed allocation is the client's no_memory error (or, for a selection
# offer, a selection the device is told is empty); the compositor lives on,
# and once the faults are spent every chain succeeds.
client="$IMWAY_TESTS_BIN/client_resource_fault"

for mode in $resource_fault_modes; do
    "$client" "$mode" fault || { echo "$mode: the failed allocation did not reach the client as expected"; exit 1; }
    expect_alive "compositor died on a failed allocation in $mode"
done

for mode in $resource_fault_modes; do
    "$client" "$mode" ok || { echo "$mode: the chain failed with its fault spent"; exit 1; }
done

expect_alive "compositor died after the faults"
echo "OK: failed allocations in $resource_fault_modes reached their clients, compositor survived"
