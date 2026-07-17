#!/usr/bin/env bash
# Drag-and-drop across two separate clients: drag from the red source
# window onto the green target window; the payload must round-trip and the
# source must see dnd_finished.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client target
wait_client "target ready"
TARGET_PID=$CLIENT_PID
TARGET_LOG=$CLIENT_LOG

CLIENT_LOG="$XDG_RUNTIME_DIR/source.log" IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/source.log" \
    start_client source
CLIENT_LOG="$XDG_RUNTIME_DIR/source.log"
wait_client "source ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

sx=$(dump_field 'app_id=dndsrc' imgx); sy=$(dump_field 'app_id=dndsrc' imgy)
tx=$(dump_field 'app_id=dndtgt' imgx); ty=$(dump_field 'app_id=dndtgt' imgy)
echo "source at $sx,$sy target at $tx,$ty"

# press on the source (it maps second, so it is on top), then drag onto the
# target's top-left corner — the one spot the source window cannot cover
ctl "motion $((sx + 100)) $((sy + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((sx + 101)) $((sy + 75))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "button left press"
wait_client "dragging"
# two motions with a frame between: the drag re-targets on last-frame hover
ctl "motion $((tx + 10)) $((ty + 10))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
ctl "motion $((tx + 11)) $((ty + 10))"
screenshot "$XDG_RUNTIME_DIR/_f.ppm"
CLIENT_LOG="$TARGET_LOG" wait_client "target entered"
ctl "button left release"

wait_client "source finished"
CLIENT_LOG="$TARGET_LOG" wait_client "target received"
wait "$TARGET_PID" || { echo "target failed"; cat "$TARGET_LOG"; exit 1; }
expect_client_ok "cross-client dnd broke"
echo "OK: dnd crossed client boundaries with the payload intact"
