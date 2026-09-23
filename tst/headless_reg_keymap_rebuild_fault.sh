#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS="xkb-keymap=4 xkb-state=1 keymap-file=2"
# A keymap rebuild that fails half way (no xkb state for the new keymap,
# no file for it, no keymap from the new layouts nor from the defaults)
# keeps the keymap in use: the layouts stay as they were, keys still
# reach clients, and a rebuild that goes through still takes.
# The boot spends one compilation, one state and two file steps (memfd,
# write); the first rebuild then loses its state, the second its memfd,
# the third goes through, and every compilation after that fails.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

layout() { dump_state | awk '/^layout/ { print $2 "/" $4 }'; }
layout_is() { [[ "$(layout)" == "$1" ]]; }
kept() { [[ "$(grep -c "imway: keeping the current keymap" "$IMWAY_LOG" || true)" -eq "$1" ]]; }

await 50 layout_is EN/count=2 || { echo "unexpected initial layout $(layout)"; exit 1; }

ctl "set keyboard.layouts de,us"
await 50 kept 1 || { echo "the stateless rebuild was not refused"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: keymap unusable: no xkb state for it" || { echo "the missing state was not named"; cat "$IMWAY_LOG"; exit 1; }
layout_is EN/count=2 || { echo "a keymap without state took over: $(layout)"; exit 1; }

ctl "set keyboard.layouts de,fr"
await 50 kept 2 || { echo "the fileless rebuild was not refused"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: keymap unusable: its file cannot be written" || { echo "the missing file was not named"; cat "$IMWAY_LOG"; exit 1; }
layout_is EN/count=2 || { echo "a keymap without a file took over: $(layout)"; exit 1; }

ctl "set keyboard.layouts de,us"
await 50 layout_is GE/count=2 || { echo "a rebuild that went through did not take: $(layout)"; exit 1; }

ctl "set keyboard.layouts us"
await 50 kept 3 || { echo "the uncompilable rebuild was not refused"; cat "$IMWAY_LOG"; exit 1; }
in_log "imway: keymap unusable: no keymap compiles" || { echo "the failed compilation was not named"; cat "$IMWAY_LOG"; exit 1; }
layout_is GE/count=2 || { echo "the kept keymap changed: $(layout)"; exit 1; }

# the probe lives beside the shared clients; this scenario has none of its own
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_input_health_probe"
input_health_probe
expect_alive "the compositor died over a failed keymap rebuild"
echo "OK: failed keymap rebuilds keep the keymap in use"
