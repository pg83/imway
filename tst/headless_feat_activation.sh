#!/usr/bin/env bash
# xdg-activation: a token activates a background toplevel, moving focus to it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_mapped
ctl "key 30 press"   # a serial for the activation token
ctl "key 30 release"

expect_client_ok "activation did not move focus"
echo "OK: xdg-activation moved keyboard focus to the activated toplevel"
