#!/usr/bin/env bash
# An input region made of the whole surface, requests that change nothing
# (an add with no height, an add clamped to nothing at the edge of the
# coordinate space, a subtract with no width) and a subtract of the top-left
# quarter that starts outside the surface: the pointer enters the window
# everywhere but that quarter.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

state() { cat "$XDG_RUNTIME_DIR/pointer-state" 2>/dev/null || true; }

# aim at (x,y) inside the window until the client reports <want>, re-reading
# the window's place and rendering a frame after each move
aim() { # <x> <y> <want>
    local i ox oy

    for ((i = 0; i < 40; i++)); do
        ox=$(dump_field 'app_id=input-region-edges' imgx)
        oy=$(dump_field 'app_id=input-region-edges' imgy)
        ctl "motion $((ox + $1)) $((oy + $2))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        ctl "motion $((ox + $1 + 1)) $((oy + $2))"
        screenshot "$XDG_RUNTIME_DIR/_f.ppm"
        [[ "$(state)" == "$3" ]] && return 0
    done

    echo "the pointer at $1,$2 of the window is $(state), not $3"
    dump_state
    exit 1
}

start_client
wait_client "region set"
wait_rect 'app_id=input-region-edges'
wait_placed 'app_id=input-region-edges' || true

aim 150 110 in
aim 30 30 out
aim 30 110 in
aim 150 30 in
aim 60 50 out

expect_alive "compositor died on an input region built from edge requests"
echo "OK: the input region left out exactly its subtracted quarter"
