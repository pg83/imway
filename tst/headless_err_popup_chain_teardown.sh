#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

"$IMWAY_CLIENT"
expect_alive "compositor died on the popup chain teardown"

# the protocol error must not leave captured grab/focus state behind
kb=$(dump_field "captured" "kb")
ptr=$(dump_field "captured" "ptr")
[[ "$kb" == "0" && "$ptr" == "0" ]] || {
    echo "grab state leaked after popup chain error: kb=$kb ptr=$ptr"
    exit 1
}

"$(dirname "$IMWAY_CLIENT")/client_health_probe"
expect_alive "compositor stopped serving after the popup chain error"
