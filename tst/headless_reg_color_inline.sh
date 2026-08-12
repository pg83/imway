#!/usr/bin/env bash
# imway-args: --hdr 203
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "managed"
sleep 0.3

bytes=$(dump_state | awk -F= '/^color_intermediate_bytes=/{print $2}')
[[ "$bytes" == 0 ]] || {
    echo "managed surface allocated $bytes bytes of conversion images"
    exit 1
}

echo "OK: managed surfaces convert inline without intermediate images"
