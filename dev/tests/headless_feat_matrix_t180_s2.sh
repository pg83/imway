#!/usr/bin/env bash
# Render matrix combo: scale 2, rotate 180, damage in buffer coords
exec "$(dirname "$0")/matrix_run.sh" 2 2 none buffer
