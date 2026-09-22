#!/usr/bin/env bash
# Requests whose answer the client checks itself: popups dismissed with an
# unmapped parent (with the v3 positioner parent hints) and the wm_base
# destroyed after its objects, a minimized v6 toplevel told it is suspended,
# a foreign-toplevel list bound after a map and stopped, the data-control
# loopback offer received, a toplevel-drag object destroyed before a drag,
# an input method's keyboard grab released and taken again, its popup
# surface object destroyed before it, the active text input destroyed (after
# a change cause the method must see), an exported toplevel destroyed under
# its import, set_parent_of on a surface that is no toplevel (a protocol
# error), a pool resized to its own size with wl_shm released, and a second
# get_release before a commit replacing the first, scale and transform
# changed on content already shown (a sync child's taken from its cache),
# a source crop under every side-swapping transform, sync grandchildren
# (one stacked under its parent subsurface) applied only by the toplevel's
# commit, a commit-timing target on a desync subsurface, and requests on a
# subsurface whose wl_surface is gone, and window-state requests sent twice
# (the second changes nothing) with a minimize of the unfocused window, a
# tearing hint on a control whose surface is gone, and the offers a
# data-control source keeps (64 types, none too long, none after use).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_wl_misc"
next() { ctl "key 2 press"; ctl "key 2 release"; } # KEY_1: the client's next step

for mode in popups suspended foreign-list dc-receive toplevel-drag im-grab im-popup text-input \
            foreign-gone foreign-bad-parent shm release rescale vp-transforms \
            nested timed-subsurface inert-subsurface state-repeats \
            tearing-dead-surface dc-offer-limits; do
    "$IMWAY_CLIENT" "$mode" || { echo "$mode failed"; exit 1; }
    expect_alive "the compositor died in $mode"
done
echo "OK: every self-checked request was answered as the protocol says"
