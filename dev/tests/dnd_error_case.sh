start_client
wait_client "mapped"
sleep 0.2
screenshot "$XDG_RUNTIME_DIR/_dnd.ppm"
imgx=$(dump_field 'app_id=dnd-error' imgx)
imgy=$(dump_field 'app_id=dnd-error' imgy)
ctl "motion $((imgx + 100)) $((imgy + 80))"
screenshot "$XDG_RUNTIME_DIR/_dnd.ppm"
ctl "motion $((imgx + 101)) $((imgy + 80))"
screenshot "$XDG_RUNTIME_DIR/_dnd.ppm"
ctl "button left press"
wait_client "dragging"
ctl "motion $((imgx + 105)) $((imgy + 82))"
expect_client_ok "malformed drag request was accepted"
ctl "button left release"
expect_alive "compositor died on malformed drag request"
input_health_probe
