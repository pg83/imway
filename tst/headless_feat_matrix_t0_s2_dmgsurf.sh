#!/usr/bin/env bash
# Render matrix combo: scale 2, no transform, damage in surface coords
exec "$(dirname "$0")/matrix_run.sh" 2 0 none surface
