# Shared tail for hostile-client scenarios: run the client, allow 77 = SKIP
# (optional capability missing), then require a live compositor that still
# serves a healthy canary client.
rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: required capability unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "hostile client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on a hostile case"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after a hostile case"
