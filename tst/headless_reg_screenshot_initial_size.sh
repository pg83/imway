#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# The 1280x800 output becomes a 640x400 viewport at the cropper's initial 50%
# zoom. The native window adds only the 200px panel and 8px inter-child spacing;
# the viewport's right and bottom edges are the native window edges.
ctl "key 99 press"
ctl "key 99 release"

await 100 in_log "toplevel imway screenshot (imway-screenshot) mapped"
wait_rect "title=imway screenshot"

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
# Grow the native window from its bottom-right edge, then invoke the same reset
# path as the button through its 0 shortcut. Besides restoring 50% zoom/scroll,
# Reset must request the exact initial native size again.
#
# The corner-grip pick runs on last-frame hover, and a loaded rasterizer
# grabs the bottom edge instead of the corner, which grows the height alone.
# Forcing frames between the motion and the press makes that rare, not
# impossible, so the whole grab is retried from wherever the corner is now.
grown() {
    rw=$(dump_field "title=imway screenshot" client_w)
    rh=$(dump_field "title=imway screenshot" client_h)

    [[ -n "$rw" && -n "$rh" ]] && (( rw > 900 && rh > 430 ))
}

drag_corner() {
    local x y ow oh gx gy d

    x=$(dump_field "title=imway screenshot" x)
    y=$(dump_field "title=imway screenshot" y)
    ow=$(dump_field "title=imway screenshot" w)
    oh=$(dump_field "title=imway screenshot" h)
    [[ -n "$x" && -n "$ow" ]] || return 1
    gx=$((x + ow - 1))
    gy=$((y + oh - 1))

    ctl "motion $gx $gy"
    screenshot "$XDG_RUNTIME_DIR/_grip.ppm"
    ctl "motion $gx $gy"
    screenshot "$XDG_RUNTIME_DIR/_grip.ppm"
    ctl "button left press"
    sleep 0.2

    for d in 20 40 60 80 100; do
        ctl "motion $((gx + d)) $((gy + d / 2))"
        screenshot "$XDG_RUNTIME_DIR/_grip.ppm" # one consumed drag step per frame
    done

    ctl "button left release"

    # transactional resize: the size lands when the viewer answers the drag's
    # configure with a buffer, and a loaded rasterizer answers late
    await 30 grown
}

rw=0
rh=0
resized=0

for _ in $(seq 1 6); do
    drag_corner && { resized=1; break; }
done

(( resized )) || {
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
