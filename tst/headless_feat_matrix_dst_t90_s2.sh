#!/usr/bin/env bash
# Render matrix combo: viewport upscale x2, scale 2, rotate 90
exec "$(dirname "$0")/matrix_run.sh" 2 1 dst buffer
