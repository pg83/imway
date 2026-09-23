#!/usr/bin/env bash
# xdg-shell, subsurface and surface requests in the wrong state: an attach
# offset on a v5 surface, sizes that conflict only across commits, a resize
# with no edge, an ack of a configure that a later ack dropped, roles asked
# of surfaces that have one or are gone, popups on dead parents, a grab on a
# mapped popup, popups on an unmapped or missing parent. Each gets its
# protocol error; an unauthorized grab on a parentless popup is dismissed,
# and a window whose wl_surface is gone still goes fullscreen.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

for mode in attach-offset-y max-under-min resize-edge-none ack-skipped-serial parent-size-flat \
            xdg-on-subsurface xdg-on-shown-surface subsurface-twice toplevel-on-popup \
            popup-twice toplevel-dead-surface popup-dead-surface popup-parent-dead-surface \
            xdg-before-popup reposition-no-anchor grab-mapped-popup grab-popup-mapped-late \
            grab-no-parent popup-on-unmapped-popup popup-no-parent-commit fullscreen-dead-surface; do
    "$IMWAY_CLIENT" "$mode" || { echo "wrong/no outcome for $mode"; exit 1; }
    expect_alive "compositor died on $mode"
done

echo "OK: requests in the wrong state got their protocol errors"
