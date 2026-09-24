#!/usr/bin/env bash
# The inert second input method on a seat commits while the active one has
# a string staged: nothing reaches the text input until the active method
# commits it itself.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "inert commit done"
expect_client_ok "the inert input method's commit flushed the active one"
expect_alive "compositor died on an inert input method's commit"
echo "OK: an inert input method's commit does nothing"
