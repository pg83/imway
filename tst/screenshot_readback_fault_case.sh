# The device runs out of memory building the screenshot capture's readback
# buffer, at the step the scenario's IMWAY_CHAOS=shot-readback=K names (0
# the buffer, 1 its memory, 2 the bind, 3 the map; a display that cannot
# import the GPU's buffers scans out dumb buffers, so every capture is read
# back instead of handed off). The Print key
# must not cost the session: the failure is reported, the next frame
# builds the buffer and captures again, and the file is saved.
shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name readback"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 100 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "imway: screenshot readback buffer failed (-2)" || { echo "the failed readback buffer was not reported"; cat "$IMWAY_LOG"; exit 1; }
retried() {
    [[ $(grep -c "imway: screenshot readback$" "$IMWAY_LOG") -ge 2 ]]
}
await 100 retried || { echo "the capture was not submitted again"; cat "$IMWAY_LOG"; exit 1; }
await 200 test -s "$shots/readback.png" || { echo "the retried capture was not saved"; cat "$IMWAY_LOG"; exit 1; }
[[ "$(head -c 4 "$shots/readback.png" | od -An -tx1 | tr -d ' \n')" == 89504e47 ]] || { echo "readback.png is not a PNG"; exit 1; }

expect_alive "compositor died on a failed screenshot readback buffer"
echo "OK: a failed readback buffer is reported, built again and the capture saved"
