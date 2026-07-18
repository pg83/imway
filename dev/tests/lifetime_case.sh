# Shared tail for buffer-lifetime scenarios: run the client, allow 77 = SKIP
# (optional capability missing), then require a live compositor that still
# serves a healthy canary client.
rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: required capability unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "lifetime client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on a buffer lifetime case"
"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after a buffer lifetime case"
