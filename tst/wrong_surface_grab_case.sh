start_client
wait_client "wrong grab ready"
wait_rect 'app_id=wrong-grab-origin'
wait_rect 'app_id=wrong-grab-victim'

# The baseline is only a baseline once the layout has stopped moving. A
# window still settling into its decorations reads as one this test moved,
# which is the opposite of what it is here to catch.
geometry() {
    dump_state | awk '/app_id=wrong-grab-(origin|victim)/ {
        for (i = 1; i <= NF; i++) if ($i ~ /^(x|y|w|h)=/) printf "%s ", $i
        printf "\n"
    }'
}

settled() {
    local first second
    first=$(geometry)
    sleep 0.2
    second=$(geometry)
    [[ -n "$first" && "$first" == "$second" ]]
}

await 50 settled || { echo "the windows never settled"; dump_state; exit 1; }

ox=$(dump_field 'app_id=wrong-grab-origin' x); oy=$(dump_field 'app_id=wrong-grab-origin' y)
ow=$(dump_field 'app_id=wrong-grab-origin' w); oh=$(dump_field 'app_id=wrong-grab-origin' h)
vx=$(dump_field 'app_id=wrong-grab-victim' x); vy=$(dump_field 'app_id=wrong-grab-victim' y)
vw=$(dump_field 'app_id=wrong-grab-victim' w); vh=$(dump_field 'app_id=wrong-grab-victim' h)
point_at_color 255 0 0 || { echo "grab origin not found"; exit 1; }
read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 255 0 0)
ctl "motion $x $y"; screenshot "$XDG_RUNTIME_DIR/_grab.ppm"
ctl "motion $((x + 1)) $y"; screenshot "$XDG_RUNTIME_DIR/_grab.ppm"
ctl "button left press"
wait_client "wrong .* requested"
target=$(awk '/wrong (move|resize) requested/ { sub(/^victim=/, "", $4); print $4; exit }' "$CLIENT_LOG")
if [[ $target == wrong-grab-origin ]]; then
    bx=$ox; by=$oy; bw=$ow; bh=$oh
else
    bx=$vx; by=$vy; bw=$vw; bh=$vh
fi
ctl "motion $((x + 80)) $((y + 60))"
sleep 0.4
ctl "button left release"
sleep 0.3
[[ $(dump_field "app_id=$target" x) == "$bx" &&
   $(dump_field "app_id=$target" y) == "$by" &&
   $(dump_field "app_id=$target" w) == "$bw" &&
   $(dump_field "app_id=$target" h) == "$bh" ]] || {
    echo "serial from another surface moved/resized victim"; exit 1; }
kill "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
input_health_probe
