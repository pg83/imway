#!/usr/bin/env bash
# Render matrix combo: scale 1, flipped+180, damage in buffer coords
exec "$(dirname "$0")/matrix_run.sh" 1 6 none buffer
