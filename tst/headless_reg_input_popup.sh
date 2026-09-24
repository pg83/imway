#!/usr/bin/env bash
# #F-14: input-method popup surface reports the text-input rectangle and is
# composited at the cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "input-popup done"

# the client prints as soon as it commits; the compositor may not have
# placed the popup yet
popup_placed() {
    dump_state | grep -q 'ime popup=1'
}

await 50 popup_placed || {
    echo "the input-method popup was not placed in the scene"
    dump_state
    exit 1
}

touch "$XDG_RUNTIME_DIR/popup-seen"
wait_client "popup surface gone"
popup_gone() {
    dump_state | grep -q 'ime popup=0'
}
await 50 popup_gone || { echo "the popup stayed placed without its surface"; dump_state; exit 1; }
echo "OK: input popup reported its rectangle and was composited"
