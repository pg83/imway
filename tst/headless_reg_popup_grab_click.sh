#!/usr/bin/env bash
# A click on a grabbing popup reaches the popup and leaves it open, even with
# a plain child popup and a popup that never mapped stacked above it; the
# next click, on the parent window, dismisses it.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "grab-click mapped"
wait_rect 'app_id=popup-grab-click'
point_at_color 255 0 0 || { echo "red window not found"; exit 1; }
sleep 0.2

# the press arms the grab serial; the client opens its popups meanwhile
ctl "button left press"
wait_client "popups open"
ctl "button left release"

for _ in $(seq 1 20); do
    # re-read the popup every try: it settles over the first frames
    px=$(dump_field '^popup mapped=1 grab=1' imgx)
    py=$(dump_field '^popup mapped=1 grab=1' imgy)
    [[ -n "$px" && -n "$py" ]] || { sleep 0.2; continue; }
    ctl "motion $((px + 50)) $((py + 40))"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
    ctl "motion $((px + 51)) $((py + 40))"
    grep -q "pointer on the popup" "$CLIENT_LOG" && break
    sleep 0.2
done
wait_client "pointer on the popup"

ctl "button left press"
ctl "button left release"
wait_client "popup clicked, still open"

x=$(dump_field 'app_id=popup-grab-click' imgx)
y=$(dump_field 'app_id=popup-grab-click' imgy)
w=$(dump_field 'app_id=popup-grab-click' client_w)
h=$(dump_field 'app_id=popup-grab-click' client_h)
for _ in $(seq 1 20); do
    ctl "motion $((x + w - 20)) $((y + h - 20))"
    screenshot "$XDG_RUNTIME_DIR/_hover.ppm"
    ctl "motion $((x + w - 21)) $((y + h - 20))"
    grep -q "pointer on the window" "$CLIENT_LOG" && break
    sleep 0.2
done
wait_client "pointer on the window"
ctl "button left press"
ctl "button left release"
wait_client "popup dismissed by a click on the window"

expect_client_ok "the grabbing popup mishandled a click"
expect_alive "compositor died clicking around a grabbing popup"
echo "OK: a click on the grabbing popup keeps it, a click on the window dismisses it"
