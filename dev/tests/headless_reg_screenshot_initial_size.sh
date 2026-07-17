#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# The 1280x800 output becomes a 640x400 viewport at the cropper's initial 50%
# zoom. The native window adds only the 200px panel and 8px inter-child spacing;
# the viewport's right and bottom edges are the native window edges.
ctl "key 99 press"
ctl "key 99 release"

await 100 in_log "toplevel imway screenshot () mapped"

w=$(dump_field "title=imway screenshot" client_w)
h=$(dump_field "title=imway screenshot" client_h)

[[ "$w $h" == "848 400" ]] || {
    echo "screenshot opened at ${w}x${h}, expected 848x400"
    dump_state
    exit 1
}

ctl "key 1 press"
ctl "key 1 release"

await 100 in_log "toplevel imway screenshot destroyed"
