#!/usr/bin/env bash
# Activation tokens authorized by a key press activate only a mapped
# window: spent on a bare wl_surface or on a toplevel that never mapped they
# do nothing, the same serial's token on the mapped window activates it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "ready"
ctl "key 30 press"
ctl "key 30 release"
wait_client "spent"
expect_client_ok "the activation client failed"

token() { sed -n "s/^token $1 //p" "$CLIENT_LOG"; }
activated() { grep -qF "imway: activation ($1)" "$IMWAY_LOG"; }

await 50 activated "$(token mapped)" || { echo "the mapped window's authorized token did not activate it"; cat "$IMWAY_LOG"; exit 1; }
activated "$(token bare)" && { echo "a token spent on a bare surface activated something"; exit 1; }
activated "$(token unmapped)" && { echo "a token spent on an unmapped toplevel activated it"; exit 1; }

expect_alive "compositor died spending tokens on no window"
echo "OK: authorized tokens activate only a mapped window"
