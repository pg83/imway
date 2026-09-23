#!/usr/bin/env bash
# The top bar names the focused window by its app_id, and one longer than
# the bar keeps is cut to the first 63 characters rather than overrunning.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "long app id set"
wait_mapped

bar_id() { dump_field '^bar ' app_id; }
want=$(python3 -c "print(''.join(chr(97 + i % 26) for i in range(63)))")
cut() { [[ "$(bar_id)" == "$want" ]]; }
await 100 cut || { echo "the bar's app_id is not the first 63 characters: $(dump_state | grep '^bar ')"; exit 1; }

touch "$XDG_RUNTIME_DIR/done-go"
expect_client_ok "the client failed"
expect_alive "compositor died naming a window with a long app_id"
echo "OK: a long app_id is cut to what the bar keeps"
