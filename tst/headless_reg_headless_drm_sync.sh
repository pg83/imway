#!/usr/bin/env bash
# imway-env: IMWAY_DRI_DIR=./dri
# imway-pre: t=; for n in /dev/dri/renderD* /dev/dri/card*; do if [ -e "$n" ]; then t=$n; break; fi; done; mkdir -p dri; ln -s /dev/null dri/renderD128; if [ -n "$t" ]; then ln -s "$t" dri/renderD129; fi
# The first render node on offer is no drm device at all, the second a real
# one with timeline syncobjs: the compositor keeps the first only as a
# fallback, takes the explicit-sync node as its identity when it comes and
# lets the fallback go. Explicit sync is offered, and the lease device and
# the dmabuf feedback name the real node.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

node=$(readlink "$XDG_RUNTIME_DIR/dri/renderD129" || true)
[[ -n "$node" ]] || { echo "SKIP: this host has no drm node to offer"; exit 127; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_headless_drm"
start_client "$node" sync
rc=0
wait "$CLIENT_PID" || rc=$?
if [[ $rc -eq 3 ]]; then
    echo "SKIP: $(cat "$CLIENT_LOG")"
    exit 127
fi
[[ $rc -eq 0 ]] || { echo "the explicit-sync node is not the compositor's identity (rc=$rc)"; cat "$CLIENT_LOG"; exit 1; }
grep -q "main device matches" "$CLIENT_LOG" || { echo "the client said nothing"; cat "$CLIENT_LOG"; exit 1; }

expect_alive "compositor died picking the explicit-sync node"
echo "OK: the explicit-sync node wins over an earlier fallback"
