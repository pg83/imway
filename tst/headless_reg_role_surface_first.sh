#!/usr/bin/env bash
# wl_surfaces destroyed before their xdg roles while mapped, a popup's and
# then a window's: frames go on without them, the inert roles go later
# without an error, and the compositor keeps serving.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/hostile_case.sh"
