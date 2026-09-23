#!/usr/bin/env bash
# imway-env: IMWAY_SHM_TRACE=1
# A surface whose sealed wl_shm pool was sampled in place as a udmabuf
# image moves on to an ordinary pool that has to be copied: the image of
# the old pool is let go and the copy is what is drawn, blue, not the
# green the imported pool still holds.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

if ! in_log "wl_shm gates image=1"; then
    echo "SKIP: no udmabuf image sampling on this host"
    exit 127
fi

start_client
wait_client "sealed committed"
wait_rect 'title=sealed-then-plain'

colour_at_centre() { # <r> <g> <b>
    local x y
    x=$(dump_field 'title=sealed-then-plain' imgx); y=$(dump_field 'title=sealed-then-plain' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/stp.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/stp.ppm" "$((x + 100))" "$((y + 75))" "$@" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y, r, g, b = map(int, sys.argv[2:7])
p = (y * w + x) * 3
sys.exit(0 if all(abs(d[p + i] - v) <= 40 for i, v in enumerate((r, g, b))) else 1)
PY
}

await 100 colour_at_centre 0 255 0 || { echo "the sealed pool was not drawn"; exit 1; }

if ! in_log "wl_shm backend udmabuf-image"; then
    echo "SKIP: the sealed pool was not sampled in place here"
    exit 127
fi

copies() { grep -c "wl_shm backend cpu" "$IMWAY_LOG" || true; }
before=$(copies)
ctl "key 30 press"; ctl "key 30 release" # KEY_A
wait_client "plain committed"
await 100 colour_at_centre 0 0 255 || { echo "the plain pool after the sampled one was not drawn"; exit 1; }
[[ "$(copies)" -gt "$before" ]] || { echo "the plain pool did not go through the copy"; exit 1; }

expect_alive "compositor died moving from a sampled to a copied pool"
echo "OK: a surface moves from a sampled sealed pool to a copied plain one"
