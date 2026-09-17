#!/usr/bin/env bash
# xdg_toplevel.resize: the application asks for the drag itself, from a grip
# of its own drawing, and the compositor follows the pointer from there. The
# compositor's own border drag is a different path and the only one tested.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_feat_interactive_resize"

start_client client-resize
wait_client "resize client mapped"
wait_rect 'app_id=resize'

cw=$(dump_field 'app_id=resize' client_w)
ch=$(dump_field 'app_id=resize' client_h)

asked() { grep -q "resize asked" "$CLIENT_LOG"; }

# The press has to land on the client, and pointer focus is worked out from
# a rendered frame, so keep aiming until the client has asked. The rect is
# re-read each time: the window is still settling into its decorations
# while this runs.
for _ in $(seq 1 25); do
    x=$(dump_field 'app_id=resize' imgx)
    y=$(dump_field 'app_id=resize' imgy)

    if [[ -n "$x" && -n "$y" ]]; then
        ctl "motion $((x + 40)) $((y + 40))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        ctl "motion $((x + 41)) $((y + 40))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        ctl "button left press"
        sleep 0.4
        asked && break
        ctl "button left release"
    fi

    sleep 0.4
done

wait_client "resize asked"

# drag away from the corner the client named, then let go
for d in 20 40 60 80; do
    ctl "motion $((x + 41 + d)) $((y + 40 + d / 2))"
    screenshot "$XDG_RUNTIME_DIR/_f.ppm"
done

ctl "button left release"

grew() {
    local nw nh
    nw=$(dump_field 'app_id=resize' client_w)
    nh=$(dump_field 'app_id=resize' client_h)
    [[ -n "$nw" && -n "$nh" ]] && (( nw > cw && nh > ch ))
}

await 100 grew || {
    echo "the client-driven resize did not reach the surface: was ${cw}x${ch}, now $(dump_field 'app_id=resize' client_w)x$(dump_field 'app_id=resize' client_h)"
    exit 1
}

expect_alive "compositor died following a client-driven resize"
echo "OK: xdg_toplevel.resize hands the drag to the compositor"
