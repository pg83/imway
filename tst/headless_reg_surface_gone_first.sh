#!/usr/bin/env bash
# A toplevel and its popup whose wl_surfaces go before their xdg objects stay
# listed without surface fields, and go once the client destroys the role
# objects as well.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

go() { touch "$XDG_RUNTIME_DIR/go-$1"; }
tl_line() { dump_state | grep '^toplevel .*title=surface-gone-first'; }
popup_line() { dump_state | grep '^popup '; }

start_client
wait_client "stage mapped"
tl_line | grep -q ' imgx=' || { echo "the toplevel was listed without its surface"; dump_state; exit 1; }
popup_line | grep -q ' imgx=' || { echo "the popup was listed without its surface"; dump_state; exit 1; }
go mapped

wait_client "stage surfaces-gone"
surfaceless() {
    local t p
    t=$(tl_line) || return 1
    p=$(popup_line) || return 1
    [[ "$t" != *" imgx="* && "$p" != *" imgx="* ]]
}
await 50 surfaceless || { echo "the toplevel and popup did not lose their surfaces in the listing"; dump_state; exit 1; }
go surfaces-gone

wait_client "roles gone"
expect_client_ok "the surfaceless client failed"
gone() { ! tl_line >/dev/null && ! popup_line >/dev/null; }
await 50 gone || { echo "the role objects' windows stayed"; dump_state; exit 1; }
expect_alive "compositor died with windows whose surfaces went first"
echo "OK: windows whose surfaces go first stay surfaceless until their roles go"
