#!/usr/bin/env bash
# imway-args: --scale 1.5
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# ScaleAllSizes(1.5) turns the default padding/spacing into 12px. The image
# zoom remains view-only and therefore stays 640x400.
ctl "key 99 press"
ctl "key 99 release"

await 100 in_log "toplevel imway screenshot () mapped"

w=$(dump_field "title=imway screenshot" client_w)
h=$(dump_field "title=imway screenshot" client_h)

[[ "$w $h" == "976 424" ]] || {
    echo "scaled screenshot opened at ${w}x${h}, expected 976x424"
    dump_state
    exit 1
}

ctl "key 1 press"
ctl "key 1 release"

await 100 in_log "toplevel imway screenshot destroyed"
