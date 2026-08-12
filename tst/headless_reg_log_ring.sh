#!/usr/bin/env bash
# The log ring data structure against its reference model (unit fuzz; the
# compositor the harness boots is unused).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
expect_client_ok "log ring fuzz failed"
echo "OK: log ring matches the reference model"
