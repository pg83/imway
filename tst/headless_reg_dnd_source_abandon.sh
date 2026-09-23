#!/usr/bin/env bash
# A drag from one client onto another client's window whose source is
# destroyed while the target has it: the target sees the drag leave.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_dnd_two_clients"

start_client target-leave
wait_client "target ready"
TARGET_PID=$CLIENT_PID
TARGET_LOG=$CLIENT_LOG

CLIENT_LOG="$XDG_RUNTIME_DIR/source.log" IMWAY_CLIENT_LOG="$XDG_RUNTIME_DIR/source.log" \
    start_client source-abandon
CLIENT_LOG="$XDG_RUNTIME_DIR/source.log"
wait_client "source ready"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/_f.ppm"

wait_rect 'app_id=dndsrc'
sx=$(dump_field 'app_id=dndsrc' imgx); sy=$(dump_field 'app_id=dndsrc' imgy)
wait_rect 'app_id=dndtgt'
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
touch "$XDG_RUNTIME_DIR/go-abandon"
wait_client "source abandoned"
CLIENT_LOG="$TARGET_LOG" wait_client "target left"
ctl "button left release"
wait "$TARGET_PID" || { echo "target failed"; cat "$TARGET_LOG"; exit 1; }
expect_client_ok "the abandoning source failed"
input_health_probe
echo "OK: a drag source destroyed over another client's window leaves it"
