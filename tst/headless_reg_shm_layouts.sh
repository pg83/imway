#!/usr/bin/env bash
# Unusual wl_shm layouts on whichever backend the device picks.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/shm_layouts_case.sh"
