#!/usr/bin/env bash
# Render matrix combo: scale 3, no transform, damage in surface coords
exec "$(dirname "$0")/matrix_run.sh" 3 0 none surface
