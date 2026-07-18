start_client
wait_client "ready"
point_at_color 255 0 0 || { echo "dnd version window not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_v.ppm"
ctl "button left press"
wait_client "dragging"
ctl "motion $((x + 4)) $((y + 2))"
wait_client "entered"
ctl "button left release"
expect_client_ok "data-device version gating failed"
input_health_probe
