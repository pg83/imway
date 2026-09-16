#!/usr/bin/env bash
# private-session-bus
# The appmenu registrar: a window registers its menu, the registrar lists it,
# refuses to answer for a window nobody registered, forgets it on request,
# and the menu model drops every property the app takes away again.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "window registered"
wait_client "layout served 1"
wait_client "menus listed"
wait_client "stranger refused"
wait_client "properties removed"
wait_client "menu registrar done"
expect_client_ok "the menu registrar client failed"
expect_alive "compositor died talking to the appmenu registrar"
echo "OK: the registrar registers, lists, refuses and forgets"
