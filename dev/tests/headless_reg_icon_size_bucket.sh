#!/usr/bin/env bash
# The store rasterizes an SVG at the desired size rounded up to a power of
# two, clamped to [16, 512], so nearby ui sizes share one raster instead of
# minting a fresh bitmap each. findIcon carries the desired edge; the dump
# resolves the window icon at whatever icon-size sets.
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/icons/hicolor/scalable/apps
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-size-bucket.svg
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "size-bucket mapped"
wait_mapped
sleep 0.3

probe() { # <desired> -> "icon_w icon_gen"
    ctl "icon-size $1"
    sleep 0.1
    dump_state | awk '/app_id=imway-size-bucket/ {
        for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2) f[kv[1]] = kv[2]
        print f["icon_w"], f["icon_gen"]; exit }'
}

# desired -> expected bucket edge
check() { # <desired> <expected_w>
    read -r w _ < <(probe "$1")
    [[ "$w" == "$2" ]] || { echo "desired $1 rasterized at $w, expected $2"; exit 1; }
}

check 16 16
check 20 32
check 48 64
check 64 64
check 100 128
check 300 512
# below the floor and above the ceiling clamp instead of exploding the cache
check 8 16
check 900 512

# in-bucket sizes share one raster (same pool generation); a different
# bucket is a distinct raster
read -r _ g48 < <(probe 48)
read -r _ g60 < <(probe 60)
read -r _ g200 < <(probe 200)

[[ "$g48" == "$g60" ]] || { echo "48 and 60 share bucket 64 but got separate rasters ($g48 vs $g60)"; exit 1; }
[[ "$g48" != "$g200" ]] || { echo "bucket 64 and bucket 256 collapsed to one raster"; exit 1; }

echo "OK: svg icons rasterize on power-of-two buckets and dedup within a bucket"
