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

# Grow the native window from its bottom-right edge, then invoke the same reset
# path as the button through its 0 shortcut. Besides restoring 50% zoom/scroll,
# Reset must request the exact initial native size again.
x=$(dump_field "title=imway screenshot" x)
y=$(dump_field "title=imway screenshot" y)
ow=$(dump_field "title=imway screenshot" w)
oh=$(dump_field "title=imway screenshot" h)
gx=$((x + ow - 1))
gy=$((y + oh - 1))

ctl "motion $gx $gy"
sleep 0.3
ctl "button left press"
sleep 0.2

for d in 20 40 60 80 100; do
    ctl "motion $((gx + d)) $((gy + d / 2))"
    sleep 0.1
done

ctl "button left release"
sleep 0.6

rw=$(dump_field "title=imway screenshot" client_w)
rh=$(dump_field "title=imway screenshot" client_h)
(( rw > 900 && rh > 430 )) || {
    echo "screenshot window did not grow: ${rw}x${rh}"
    dump_state
    exit 1
}

ctl "key 11 press"   # KEY_0: the same resetView path as the Reset button
ctl "key 11 release"

for _ in $(seq 1 40); do
    w=$(dump_field "title=imway screenshot" client_w)
    h=$(dump_field "title=imway screenshot" client_h)
    [[ "$w $h" == "848 400" ]] && break
    sleep 0.1
done

[[ "$w $h" == "848 400" ]] || {
    echo "Reset left screenshot at ${w}x${h}, expected 848x400"
    dump_state
    exit 1
}

ctl "key 1 press"
ctl "key 1 release"

await 100 in_log "toplevel imway screenshot destroyed"
