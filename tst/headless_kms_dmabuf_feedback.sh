#!/usr/bin/env bash
# linux-dmabuf feedback over the KMS emulator leads with a scanout tranche
# of the primary plane's formats, in the default and the surface feedback.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: linux-dmabuf v4 unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "the dmabuf feedback carried no scanout tranche (rc=$rc)"; exit 1; }
expect_alive "compositor died sending dmabuf feedback"
echo "OK: dmabuf feedback led with the scanout tranche"
