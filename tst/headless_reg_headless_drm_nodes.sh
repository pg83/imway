#!/usr/bin/env bash
# imway-env: IMWAY_DRI_DIR=./dri
# imway-pre: t=; for n in /dev/dri/renderD* /dev/dri/card*; do if [ -e "$n" ]; then t=$n; break; fi; done; mkdir -p dri; if [ -n "$t" ]; then ln -s "$t" dri/renderD128; ln -s "$t" dri/renderD129; fi
# The node a headless compositor takes as its drm identity, from two render
# nodes on offer: one with timeline syncobjs is taken outright, otherwise
# the first is kept as the fallback and the second closed. The lease device
# then offers that node with no connectors, and dmabuf feedback names it as
# the main device.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

node=$(readlink "$XDG_RUNTIME_DIR/dri/renderD128" || true)
[[ -n "$node" ]] || { echo "SKIP: this host has no drm node to offer"; exit 127; }

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_headless_drm"
start_client "$node"
wait_client "drm node"
expect_client_ok "the compositor's drm identity is not the offered node"

expect_alive "compositor died picking a drm node"
echo "OK: the offered node is the compositor's drm identity"
