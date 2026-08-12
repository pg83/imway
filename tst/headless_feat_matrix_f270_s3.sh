#!/usr/bin/env bash
# Render matrix combo: scale 3, flipped+270, damage in buffer coords
exec "$(dirname "$0")/matrix_run.sh" 3 7 none buffer
