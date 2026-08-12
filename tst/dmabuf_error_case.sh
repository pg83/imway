rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: linux-dmabuf global unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "dmabuf error client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on malformed dmabuf request"
