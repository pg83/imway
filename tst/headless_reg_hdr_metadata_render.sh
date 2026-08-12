#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 600 --hdr-fall 350
set -euo pipefail
. "$(dirname "$0")/lib.sh"

metadata_is_mapped() {
    local state
    state=$(dump_state)
    [[ $(awk '/^hdr metadata=/{for(i=1;i<=NF;i++)if($i~/^max_cll=/){sub(/^max_cll=/,"",$i);print $i}}' <<<"$state") == 599 ]]
    [[ $(awk '/^hdr metadata=/{for(i=1;i<=NF;i++)if($i~/^max_fall=/){sub(/^max_fall=/,"",$i);print $i}}' <<<"$state") == 300 ]]
}

start_client
wait_client "managed"
await 50 metadata_is_mapped

echo "OK: visible client metadata follows display mapping"
