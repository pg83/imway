start_client
wait_client "mapped"
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2
ctl "button left press"
wait_client "dragging"
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $((x + 4)) $((y + 2))"
wait_client "entered"
ctl "button left release"
expect_client_ok "post-finish request did not raise the required error"
expect_alive "compositor died on a post-finish request"
input_health_probe
