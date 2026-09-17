#!/usr/bin/env bash
# wl_keyboard.enter carries the keys that are already down, so a client that
# takes the focus mid-chord starts with the right idea of what is held.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "focused"

ctl "key 30 press" # A, held for the rest of the scenario

wait_client "key held"
wait_client "enter carried"

ctl "key 30 release"
expect_client_ok "the enter did not carry the held key"
expect_alive "compositor died handing over the keyboard mid-chord"
echo "OK: wl_keyboard.enter carries the keys that were already down"
