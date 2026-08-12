#!/usr/bin/env bash
set -euo pipefail
. "$(dirname "$0")/lib.sh"
VICTIM=source PHASE=enter
. "$(dirname "$0")/dnd_kill_phases_case.sh"
