#!/usr/bin/env bash
# Render matrix combo: scale 1, no transform, damage in buffer coords
exec "$(dirname "$0")/matrix_run.sh" 1 0 none buffer
