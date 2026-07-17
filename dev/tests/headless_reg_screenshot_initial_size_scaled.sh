#!/usr/bin/env bash
# imway-args: --mode 3840x2160@60 --scale 2.5
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# Match the real desktop configuration: the 3840x2160 output becomes a
# 1920x1080 viewport at 50%, alongside a 500px panel and 20px spacing.
ctl "key 99 press"
ctl "key 99 release"

await 100 in_log "toplevel imway screenshot () mapped"

w=$(dump_field "title=imway screenshot" client_w)
h=$(dump_field "title=imway screenshot" client_h)

[[ "$w $h" == "2440 1080" ]] || {
    echo "scaled screenshot opened at ${w}x${h}, expected 2440x1080"
    dump_state
    exit 1
}

ctl "key 1 press"
ctl "key 1 release"

await 100 in_log "toplevel imway screenshot destroyed"
