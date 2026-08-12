#!/usr/bin/env bash
# An alpha-capable fullscreen dmabuf may reach direct scanout only when its
# opaque region covers the whole surface: the primary plane does not blend,
# so a translucent buffer would render differently than in composition.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client

for _ in $(seq 1 100); do
    grep -q "phase1" "$CLIENT_LOG" && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
done

if ! kill -0 "$CLIENT_PID" 2>/dev/null; then
    rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 77 ]] && { echo "SKIP: dmabuf unavailable"; exit 127; }
    echo "client died (rc=$rc)"; cat "$CLIENT_LOG"; exit 1
fi

grep -q "phase1" "$CLIENT_LOG" || { echo "client did not draw"; cat "$CLIENT_LOG"; exit 1; }
sleep 0.5

cand=$(dump_field 'scanout' candidate)
[[ "$cand" == "0" ]] || {
    echo "ARGB surface without an opaque region is a scanout candidate (id=$cand)"
    exit 1
}

ctl "key 30 press"
ctl "key 30 release"
wait_client "phase2"
sleep 0.5

cand=$(dump_field 'scanout' candidate)
[[ "$cand" != "0" ]] || {
    echo "fully opaque ARGB surface is not a scanout candidate"
    exit 1
}

echo "OK: opaque region gates ARGB direct scanout"
